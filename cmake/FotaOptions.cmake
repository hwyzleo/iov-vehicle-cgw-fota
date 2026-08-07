# cmake/FotaOptions.cmake - CGW-FOTA-DSN-CR-008 §12
#
# Composable build options for CGW-FOTA.
#
# Hard constraints (CR-008 §12.2/§12.3/§12.6):
#   * Does NOT set compiler, sysroot, target triple or host absolute paths.
#   * Coverage/sanitizer are dev/CI-only and MUST NOT change release ABI or
#     leak into install rules / exported interfaces.
#   * Warnings are composable; never alter release ABI.

option(FOTA_BUILD_TESTS "Build CGW-FOTA unit/contract tests" ON)
option(FOTA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(FOTA_ENABLE_COVERAGE "Enable code coverage instrumentation (dev/CI only)" OFF)
option(FOTA_ENABLE_SANITIZER "Enable address/undefined-behavior sanitizer (dev/CI only)" OFF)

# Coverage and sanitizer are non-release concerns. Refuse them in Release
# builds to guarantee release ABI stability (CR-008 §12.3 "warning、sanitizer、
# coverage 为可组合选项；不得改变 release ABI").
if(FOTA_ENABLE_COVERAGE OR FOTA_ENABLE_SANITIZER)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        message(WARNING
            "FOTA_ENABLE_COVERAGE/FOTA_ENABLE_SANITIZER set but CMAKE_BUILD_TYPE="
            "'${CMAKE_BUILD_TYPE}'. Instrumentation only applies to Debug/RelWithDebInfo "
            "and is ignored for Release to preserve ABI.")
    endif()
endif()
