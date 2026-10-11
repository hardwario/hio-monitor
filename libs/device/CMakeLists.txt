# libs/device - device profiles (ADR-0032), console lines and log, command history, console files,
# probe locks (ADR-0058) and DeviceSession with its worker thread (ADR-0031, ADR-0053, ADR-0063)
# (plan/features/0002-m1-jlink-rtt.md). Backend library: QtCore only (ADR-0004), static
# (ADR-0022).

hio_add_backend_library(hio_device
    include/hio/device/command_history.h
    include/hio/device/console_file_sink.h
    include/hio/device/console_line.h
    include/hio/device/console_log.h
    include/hio/device/device_session.h
    include/hio/device/line_assembler.h
    include/hio/device/probe_lock.h
    include/hio/device/probe_tools.h
    include/hio/device/profile.h
    include/hio/device/session_events.h
    include/hio/device/session_registry.h
    src/command_history.cpp
    src/console_file_sink.cpp
    src/console_line.cpp
    src/console_log.cpp
    src/device_session.cpp
    src/line_assembler.cpp
    src/probe_lock.cpp
    src/probe_tools.cpp
    src/profile.cpp
    src/profiles/chester.cpp
    src/profiles/profiles.h
    src/session_internal.h
    src/session_registry.cpp
    src/session_worker.cpp)
add_library(hio::device ALIAS hio_device)

target_include_directories(hio_device PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hio_device PUBLIC hio::core hio::hw_jlink Qt6::Core)
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
