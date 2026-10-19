# libs/diag - environment checks, the check runner, the probe-holder process scan (ADR-0073), the
# Linux USB access check (ADR-0072) and the diagnostic report (ADR-0075)
# (plan/features/0007-m4-wizard-framework-diagnostics.md, ADR-0068). Backend: QtCore only
# (ADR-0004), static (ADR-0022).
#
# Three targets (no dependency cycle with libs/device):
#   hio::diag_types  interface: check.h (CheckStatus for libs/device's DiagnoseStep, ADR-0069)
#   hio::diag_usb    static:    usb_access (DeviceSession reports usb.access_denied after a failed
#                               probe open); depends on libs/core only
#   hio::diag        static:    everything else; depends on libs/device (probe enumeration)

add_library(hio_diag_types INTERFACE)
add_library(hio::diag_types ALIAS hio_diag_types)
target_include_directories(hio_diag_types INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hio_diag_types INTERFACE hio::core)

hio_add_backend_library(hio_diag_usb
    include/hio/diag/usb_access.h
    src/usb_access.cpp)
add_library(hio::diag_usb ALIAS hio_diag_usb)
target_include_directories(hio_diag_usb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hio_diag_usb PUBLIC hio::core Qt6::Core)

hio_add_backend_library(hio_diag
    include/hio/diag/check.h
    include/hio/diag/environment.h
    include/hio/diag/process_scan.h
    include/hio/diag/report.h
    src/check.cpp
    src/environment.cpp
    src/process_scan.cpp
    src/redact.cpp
    src/report.cpp
    src/platform/process_source.h
    src/platform/process_source_linux.cpp
    src/platform/process_source_macos.cpp
    src/platform/process_source_windows.cpp)
add_library(hio::diag ALIAS hio_diag)
target_include_directories(hio_diag PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(hio_diag PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hio_diag
    PUBLIC hio::diag_types hio::diag_usb hio::core hio::device hio::hw_jlink hio::hw_nrfutil
           hio::hw_ble
           Qt6::Core
    PRIVATE hio::miniz hio::version_info)
if(WIN32)
    target_compile_definitions(hio_diag PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(HIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
