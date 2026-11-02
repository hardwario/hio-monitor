# libs/device - device profiles (ADR-0032), console lines and log, command history, console files,
# probe locks (ADR-0058) and DeviceSession with its worker thread (ADR-0031, ADR-0053, ADR-0063)
# (plan/features/0002-m1-jlink-rtt.md); M2 (plan/features/0003-m2-flashing.md): flash, erase,
# recover, modem and reset operations, workflows, PIB decoder, profile flash layout. Backend library: QtCore only (ADR-0004), static
# (ADR-0022).

hwio_add_backend_library(hwio_device
    include/hwio/device/ble_lock.h
    include/hwio/device/ble_session.h
    include/hwio/device/command_capture.h
    include/hwio/device/command_history.h
    include/hwio/device/command_tree.h
    include/hwio/device/command_tree_cache.h
    include/hwio/device/command_tree_learner.h
    include/hwio/device/completer.h
    include/hwio/device/console_file_sink.h
    include/hwio/device/console_line.h
    include/hwio/device/console_log.h
    include/hwio/device/console_session.h
    include/hwio/device/device_info.h
    include/hwio/device/device_session.h
    include/hwio/device/device_info_watcher.h
    include/hwio/device/firmware_identity.h
    include/hwio/device/line_assembler.h
    include/hwio/device/operation.h
    include/hwio/device/pib.h
    include/hwio/device/probe_lock.h
    include/hwio/device/probe_tools.h
    include/hwio/device/profile.h
    include/hwio/device/session_events.h
    include/hwio/device/session_key.h
    include/hwio/device/session_registry.h
    include/hwio/device/shell_query.h
    src/ble_lock.cpp
    src/ble_session.cpp
    src/ble_session_internal.h
    src/banner_scan.h
    src/command_capture.cpp
    src/command_history.cpp
    src/command_tree.cpp
    src/command_tree_cache.cpp
    src/command_tree_learner.cpp
    src/completer.cpp
    src/console_file_sink.cpp
    src/console_line.cpp
    src/console_log.cpp
    src/console_session.cpp
    src/device_info.cpp
    src/device_session.cpp
    src/device_info_watcher.cpp
    src/firmware_identity.cpp
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
    src/owner_only.h
    src/session_internal.h
    src/session_registry.cpp
    src/session_worker.cpp
    src/shell_query.cpp
    src/session_worker_target.cpp
    src/session_worker_target.h)
add_library(hwio::device ALIAS hwio_device)

target_include_directories(hwio_device PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hwio_device PUBLIC hwio::core hwio::firmware hwio::hw_jlink hwio::hw_nrfutil hwio::hw_ble Qt6::Core
    # M4 (plan/features/0007): CheckStatus of DiagnoseStep; usb.access_denied after a failed open.
    hwio::diag_types hwio::diag_usb)
# ADR-0039: the nRF91 disable-AP-protect image (copied verbatim from py-hardwario, see
# resources/nrf91_disable_ap_protect.NOTICE.txt), compiled in as :/hwio/device/....
qt_add_resources(hwio_device "hwio_device_resources"
    PREFIX "/hwio/device"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/nrf91_disable_ap_protect.hex"
    OUTPUT_TARGETS _hwio_device_resource_targets)
hwio_skip_linting(${_hwio_device_resource_targets})
# DeviceSession is a QObject (signals).
set_target_properties(hwio_device PROPERTIES AUTOMOC ON)
if(WIN32)
    target_compile_definitions(hwio_device PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    # GetModuleFileNameExW (ProbeLock::owner mirrors QLockFile's process-name rule, review R41).
    target_link_libraries(hwio_device PRIVATE psapi)
endif()

if(HWIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
