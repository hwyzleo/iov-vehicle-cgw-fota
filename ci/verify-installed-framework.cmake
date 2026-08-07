# ci/verify-installed-framework.cmake - CGW-FOTA-DSN-CR-008 §12.2/§12.7
#
# Verify the installed CGW Framework SDK package is relocatable and consumable
# from an isolated staging prefix: find_package(CGWFramework CONFIG REQUIRED)
# resolves and all five exported targets link into a trivial HOST consumer.
#
# HOST consumer success does NOT replace TARGET architecture/ABI/link
# verification (CR-008 §12.2); it only proves the package config + imported
# targets are valid and do not bake host/personal absolute paths.
#
# Usage:
#   cmake -DSDK_PREFIX=<framework-install-prefix> \
#         [-DCMAKE_CXX_COMPILER=<cc>] \
#         -P ci/verify-installed-framework.cmake
#
# Exit: FATAL_ERROR on any failure (non-zero for -P script mode).

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED SDK_PREFIX)
    message(FATAL_ERROR "SDK_PREFIX must point to the installed framework prefix")
endif()

set(_consumer_dir "${CMAKE_CURRENT_BINARY_DIR}/_fw_consumer")
file(REMOVE_RECURSE "${_consumer_dir}")
file(MAKE_DIRECTORY "${_consumer_dir}")

# Minimal consumer that links all five framework targets.
file(WRITE "${_consumer_dir}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.24)
project(fw_consumer LANGUAGES CXX)
find_package(CGWFramework CONFIG REQUIRED)
add_executable(fw_consumer main.cpp)
target_compile_features(fw_consumer PRIVATE cxx_std_20)
target_link_libraries(fw_consumer PRIVATE
    CGWFramework::cgw-framework-config
    CGWFramework::cgw-framework-log
    CGWFramework::cgw-framework-store
    CGWFramework::cgw-framework-hash
    CGWFramework::cgw-framework-someip)
]=])

# A consumer that only touches exported API surface (headers + symbols) enough
# to prove the imported targets resolve and link. Keep it minimal and portable.
# Header layout follows the CGW-FW installed export (flat config.h/log.h/
# store.h + cgw/fw/hash|someip/*.hpp).
file(WRITE "${_consumer_dir}/main.cpp" [==[
#include "config.h"
#include "log.h"
#include "store.h"
#include "cgw/fw/hash/sha256.hpp"
#include "cgw/fw/someip/runtime.hpp"
int main() { return 0; }
]==])

message(STATUS "[verify-installed-framework] SDK_PREFIX=${SDK_PREFIX}")

execute_process(
    COMMAND ${CMAKE_COMMAND}
            -DCMAKE_PREFIX_PATH=${SDK_PREFIX}
            -DCGWFramework_DIR=${SDK_PREFIX}/lib/cmake/CGWFramework
            -S "${_consumer_dir}" -B "${_consumer_dir}/build"
    RESULT_VARIABLE _cfg)
if(NOT _cfg EQUAL 0)
    file(REMOVE_RECURSE "${_consumer_dir}")
    message(FATAL_ERROR "[verify-installed-framework] configure failed (find_package/resolve)")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} --build "${_consumer_dir}/build" --config Debug
    RESULT_VARIABLE _build)
if(NOT _build EQUAL 0)
    file(REMOVE_RECURSE "${_consumer_dir}")
    message(FATAL_ERROR "[verify-installed-framework] build failed (compile/link 5 targets)")
endif()

file(REMOVE_RECURSE "${_consumer_dir}")
message(STATUS "[verify-installed-framework] OK: find_package + 5 targets link from isolated prefix")
