# libs/diag - environment checks, the check runner, the probe-holder process scan (ADR-0073), the
# Linux USB access check (ADR-0072) and the diagnostic report (ADR-0075)
# (plan/features/0007-m4-wizard-framework-diagnostics.md, ADR-0068). Backend: QtCore only
# (ADR-0004), static (ADR-0022).
#
# Three targets (no dependency cycle with libs/device):
#   hwio::diag_types  interface: check.h (CheckStatus for libs/device's DiagnoseStep, ADR-0069)
#   hwio::diag_usb    static:    usb_access (DeviceSession reports usb.access_denied after a failed
#                               probe open); depends on libs/core only
#   hwio::diag        static:    everything else; depends on libs/device (probe enumeration)

add_library(hwio_diag_types INTERFACE)
add_library(hwio::diag_types ALIAS hwio_diag_types)
target_include_directories(hwio_diag_types INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hwio_diag_types INTERFACE hwio::core)

hwio_add_backend_library(hwio_diag_usb
    include/hwio/diag/usb_access.h
    src/usb_access.cpp)
add_library(hwio::diag_usb ALIAS hwio_diag_usb)
target_include_directories(hwio_diag_usb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hwio_diag_usb PUBLIC hwio::core Qt6::Core)

hwio_add_backend_library(hwio_diag
    include/hwio/diag/check.h
    include/hwio/diag/environment.h
    include/hwio/diag/process_scan.h
    include/hwio/diag/report.h
    src/check.cpp
    src/environment.cpp
    src/process_scan.cpp
    src/redact.cpp
    src/report.cpp
    src/platform/process_source.h
    src/platform/process_source_linux.cpp
    src/platform/process_source_macos.cpp
    src/platform/process_source_windows.cpp)
add_library(hwio::diag ALIAS hwio_diag)
target_include_directories(hwio_diag PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(hwio_diag PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hwio_diag
    PUBLIC hwio::diag_types hwio::diag_usb hwio::core hwio::device hwio::hw_jlink hwio::hw_nrfutil
           hwio::hw_ble
           Qt6::Core
    PRIVATE hwio::miniz hwio::version_info)
if(WIN32)
    target_compile_definitions(hwio_diag PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(HWIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
