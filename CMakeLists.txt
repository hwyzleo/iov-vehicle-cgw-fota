cmake_minimum_required(VERSION 3.10)
project(CgwFotaService)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -g)

# LIB_DIR: reserved for platform-specific third-party libraries
if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
    if (APPLE)
        set(LIB_DIR ${PROJECT_SOURCE_DIR}/third_party/lib/arm64-apple-darwin)
    elseif (UNIX AND NOT APPLE)
        set(LIB_DIR ${PROJECT_SOURCE_DIR}/third_party/lib/aarch64-linux-gnu)
    endif ()
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
    if (APPLE)
        set(LIB_DIR ${PROJECT_SOURCE_DIR}/third_party/lib/x86_64-apple-darwin)
    elseif (UNIX AND NOT APPLE)
        set(LIB_DIR ${PROJECT_SOURCE_DIR}/third_party/lib/x86_64-linux-gnu)
    endif ()
endif ()

find_package(yaml-cpp REQUIRED)
include_directories(${YAML_CPP_INCLUDE_DIRS})

find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

find_package(nlohmann_json REQUIRED)

set(LIB_SOURCES
    src/error_codes.cpp
    src/data_models.cpp
    src/config_loader.cpp
    src/snapshot_assembler.cpp
    src/inventory_reporter.cpp
    src/someip_fota_client.cpp
    src/someip_tbox_client.cpp
)

# Create cgw_fota library
add_library(cgw_fota_lib STATIC ${LIB_SOURCES})
target_include_directories(cgw_fota_lib PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(cgw_fota_lib PUBLIC yaml-cpp::yaml-cpp nlohmann_json::nlohmann_json)

# Main application executable
add_executable(cgw_fota src/main.cpp)
target_include_directories(cgw_fota PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(cgw_fota PRIVATE cgw_fota_lib)

enable_testing()
add_executable(CgwFotaTests
    tests/test_data_models.cpp
    tests/test_snapshot_assembler.cpp
    tests/test_inventory_reporter.cpp
    tests/test_someip_fota_client.cpp
    tests/test_someip_tbox_client.cpp
    tests/test_config_loader.cpp
    tests/test_integration.cpp
)
target_include_directories(CgwFotaTests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(CgwFotaTests PRIVATE cgw_fota_lib GTest::GTest GTest::gmock GTest::Main)
add_test(NAME CgwFotaTests COMMAND CgwFotaTests)
