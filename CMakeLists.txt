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

# CGW Framework: 组件静态库 (CGW-FOTA-DSN-CR-004)
#   cgw-framework-config  - 六阶段配置加载/合并/校验（业务代码不再直接用 yaml-cpp）
#   cgw-framework-log     - Logger / ContextScope / 结构化字段
# yaml-cpp 经框架静态库以 LINK_ONLY 传递（仅链接、不暴露 include），满足
# “生产构建不存在业务侧 yaml-cpp 直接访问”的验收要求。
find_package(CGWFramework CONFIG REQUIRED)

find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

find_package(nlohmann_json REQUIRED)

set(LIB_SOURCES
    src/error_codes.cpp
    src/data_models.cpp
    src/config/fota_config.cpp
    src/store/fota_state.cpp
    src/store/fota_state_serializer.cpp
    src/store/fota_state_store.cpp
    src/store/fota_state_recovery.cpp
    src/snapshot_assembler.cpp
    src/inventory_reporter.cpp
    src/someip_fota_client.cpp
    src/someip_tbox_client.cpp
    src/someip_fota_provider.cpp
    src/fota_log_adapter.cpp
    src/fota_log_context.cpp
)

# Create cgw_fota library
add_library(cgw_fota_lib STATIC ${LIB_SOURCES})
target_include_directories(cgw_fota_lib PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(cgw_fota_lib PUBLIC
    CGWFramework::cgw-framework-config
    CGWFramework::cgw-framework-log
    CGWFramework::cgw-framework-store
    nlohmann_json::nlohmann_json)

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
    tests/test_fota_config.cpp
    tests/test_config_load.cpp
    tests/test_integration.cpp
    tests/test_someip_fota_provider.cpp
    tests/test_tbox_someip_integration.cpp
    tests/test_log_adapter.cpp
    tests/test_log_events.cpp
    tests/test_log_integration.cpp
    tests/test_fota_state_serializer.cpp
    tests/test_fota_state_store.cpp
    tests/test_fota_state_recovery.cpp
    tests/test_store_integration.cpp
)
target_include_directories(CgwFotaTests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(CgwFotaTests PRIVATE cgw_fota_lib GTest::GTest GTest::gmock GTest::Main)
add_test(NAME CgwFotaTests COMMAND CgwFotaTests)

# ============================================================================
# Install (CGW-FOTA-DSN-CR-004 目录与所有权设计)
# 精确安装 fota.default.yaml 并在 staging 改名为 etc/cgw/conf.d/fota.yaml；
# 不得通配安装整个 config/。开发夹具 config/fota.yaml、config/common.yaml
# 不进入正式 install component。schema 随源码版本化并安装到 share 供 BUILD 引用。
# ============================================================================
include(GNUInstallDirs)
install(FILES config/fota.default.yaml
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/cgw/conf.d
        RENAME fota.yaml)
install(FILES config/schema/fota.schema.yaml
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/cgw/fota/schema)
install(TARGETS cgw_fota_lib cgw_fota
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
