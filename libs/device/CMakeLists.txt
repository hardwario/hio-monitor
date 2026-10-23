# libs/device - device profiles (ADR-0032), console lines and log, command history, console files,
# probe locks (ADR-0058) and DeviceSession with its worker thread (ADR-0031, ADR-0053, ADR-0063)
# (plan/features/0002-m1-jlink-rtt.md); M2 (plan/features/0003-m2-flashing.md): flash, erase,
# recover, modem and reset operations, workflows, PIB decoder, profile flash layout. Backend library: QtCore only (ADR-0004), static
# (ADR-0022).

hio_add_backend_library(hio_device
    include/hio/device/ble_lock.h
    include/hio/device/ble_session.h
    include/hio/device/command_history.h
    include/hio/device/console_file_sink.h
    include/hio/device/console_line.h
    include/hio/device/console_log.h
    include/hio/device/console_session.h
    include/hio/device/device_session.h
    include/hio/device/line_assembler.h
    include/hio/device/operation.h
    include/hio/device/pib.h
    include/hio/device/probe_lock.h
    include/hio/device/probe_tools.h
    include/hio/device/profile.h
    include/hio/device/session_events.h
    include/hio/device/session_key.h
    include/hio/device/session_registry.h
    src/ble_lock.cpp
    src/ble_session.cpp
    src/ble_session_internal.h
    src/command_history.cpp
    src/console_file_sink.cpp
    src/console_line.cpp
    src/console_log.cpp
    src/console_session.cpp
    src/device_session.cpp
    src/flash_workflows.cpp
    src/flash_workflows.h
    src/line_assembler.cpp
    src/operation.cpp
    src/operation_reporter.cpp
    src/operation_reporter.h
    src/pib.cpp
    src/lock_file.cpp
    src/lock_file.h
    src/probe_lock.cpp
    src/probe_tools.cpp
    src/profile.cpp
    src/profiles/chester.cpp
    src/profiles/profiles.h
    src/session_internal.h
    src/session_registry.cpp
    src/session_worker.cpp
    src/session_worker_target.cpp
    src/session_worker_target.h)
add_library(hio::device ALIAS hio_device)

target_include_directories(hio_device PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hio_device PUBLIC hio::core hio::firmware hio::hw_jlink hio::hw_nrfutil hio::hw_ble Qt6::Core
    # M4 (plan/features/0007): CheckStatus of DiagnoseStep; usb.access_denied after a failed open.
    hio::diag_types hio::diag_usb)
# ADR-0039: the nRF91 disable-AP-protect image (copied verbatim from py-hardwario, see
# resources/nrf91_disable_ap_protect.NOTICE.txt), compiled in as :/hio/device/....
qt_add_resources(hio_device "hio_device_resources"
    PREFIX "/hio/device"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/nrf91_disable_ap_protect.hex"
    OUTPUT_TARGETS _hio_device_resource_targets)
hio_skip_linting(${_hio_device_resource_targets})
# DeviceSession is a QObject (signals).
set_target_properties(hio_device PROPERTIES AUTOMOC ON)
if(WIN32)
    target_compile_definitions(hio_device PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    # GetModuleFileNameExW (ProbeLock::owner mirrors QLockFile's process-name rule, review R41).
    target_link_libraries(hio_device PRIVATE psapi)
endif()

if(HIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
