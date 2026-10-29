# app/cli - the `hwio` command-line interface (ADR-0017, ADR-0018). English only (ADR-0027).
#
# hwio_cli_impl holds everything except main() so that the unit tests can link it.
# QtCore only; no translations are embedded. The interactive console uses replxx (ADR-0033).

add_library(hwio_cli_impl STATIC
    src/chester_commands.cpp
    src/chester_commands.h
    src/ble_commands.cpp
    src/ble_commands.h
    src/cli_app.cpp
    src/cli_app.h
    src/cli_log.cpp
    src/cli_log.h
    src/command_collector.cpp
    src/command_collector.h
    src/command_response.cpp
    src/command_response.h
    src/cli_device_provider.cpp
    src/cli_device_provider.h
    src/confirm.cpp
    src/confirm.h
    src/diag_commands.cpp
    src/diag_commands.h
    src/console_events.cpp
    src/console_events.h
    src/console_learning.cpp
    src/console_learning.h
    src/console_ui.cpp
    src/console_ui.h
    src/exit_codes.cpp
    src/exit_codes.h
    src/flag_consent_broker.cpp
    src/flag_consent_broker.h
    src/flash_commands.cpp
    src/flash_commands.h
    src/jlink_commands.cpp
    src/jlink_commands.h
    src/mcp_command.cpp
    src/mcp_command.h
    src/operation_runner.cpp
    src/operation_runner.h
    src/output.cpp
    src/output.h
    src/probe_options.cpp
    src/probe_options.h
    src/progress_ui.cpp
    src/progress_ui.h
    src/selftest.cpp
    src/selftest.h
    src/signals.cpp
    src/signals.h
    src/stdin_line_source.cpp
    src/stdin_line_source.h
    src/target_commands.cpp
    src/target_commands.h
    src/terminal.cpp
    src/terminal.h
    src/update_commands.cpp
    src/update_commands.h)
hwio_apply_target_defaults(hwio_cli_impl)
target_include_directories(hwio_cli_impl PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hwio_cli_impl PUBLIC
    hwio::core hwio::device hwio::diag hwio::mcp hwio::mcp_tools hwio::update Qt6::Core CLI11::CLI11)
# replxx (ADR-0033) only for the interactive console; the backend never links it.
target_link_libraries(hwio_cli_impl PRIVATE replxx::replxx)
if(WIN32)
    target_compile_definitions(hwio_cli_impl PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

qt_add_executable(hwio src/main.cpp)
# Plan 0015 A3: icon and VERSIONINFO on Windows.
if(WIN32)
    set(HWIO_RC_FILE_DESCRIPTION "HARDWARIO Monitor command-line interface")
    set(HWIO_RC_INTERNAL_NAME "hwio")
    set(HWIO_RC_ORIGINAL_FILENAME "hwio.exe")
    configure_file("${PROJECT_SOURCE_DIR}/packaging/windows/version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/hwio.rc" @ONLY)
    target_sources(hwio PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/hwio.rc")
endif()
hwio_apply_target_defaults(hwio)
target_link_libraries(hwio PRIVATE hwio_cli_impl)
# A console application on every OS (no WIN32_EXECUTABLE, no MACOSX_BUNDLE).
set_target_properties(hwio PROPERTIES WIN32_EXECUTABLE OFF MACOSX_BUNDLE OFF)
# ADR-0086: macOS needs NSBluetoothAlwaysUsageDescription before CoreBluetooth may be used; the
# bare executable carries its Info.plist as the __TEXT,__info_plist section.
if(APPLE)
    set(HWIO_CLI_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
    configure_file(macos/Info.plist.in "${HWIO_CLI_INFO_PLIST}" @ONLY)
    target_link_options(hwio PRIVATE
        "LINKER:-sectcreate,__TEXT,__info_plist,${HWIO_CLI_INFO_PLIST}")
    set_property(TARGET hwio APPEND PROPERTY LINK_DEPENDS "${HWIO_CLI_INFO_PLIST}")
endif()

if(HWIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
