# app/cli - the `hio` command-line interface (ADR-0017, ADR-0018). English only (ADR-0027).
#
# hio_cli_impl holds everything except main() so that the unit tests can link it.
# QtCore only; no translations are embedded. The interactive console uses replxx (ADR-0033).

add_library(hio_cli_impl STATIC
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
    src/confirm.cpp
    src/confirm.h
    src/diag_commands.cpp
    src/diag_commands.h
    src/console_events.cpp
    src/console_events.h
    src/console_ui.cpp
    src/console_ui.h
    src/exit_codes.cpp
    src/exit_codes.h
    src/flash_commands.cpp
    src/flash_commands.h
    src/jlink_commands.cpp
    src/jlink_commands.h
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
    src/terminal.cpp
    src/terminal.h)
hio_apply_target_defaults(hio_cli_impl)
target_include_directories(hio_cli_impl PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hio_cli_impl PUBLIC hio::core hio::device hio::diag Qt6::Core CLI11::CLI11)
# replxx (ADR-0033) only for the interactive console; the backend never links it.
target_link_libraries(hio_cli_impl PRIVATE replxx::replxx)
if(WIN32)
    target_compile_definitions(hio_cli_impl PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

qt_add_executable(hio src/main.cpp)
hio_apply_target_defaults(hio)
target_link_libraries(hio PRIVATE hio_cli_impl)
# A console application on every OS (no WIN32_EXECUTABLE, no MACOSX_BUNDLE).
set_target_properties(hio PROPERTIES WIN32_EXECUTABLE OFF MACOSX_BUNDLE OFF)
# ADR-0086: macOS needs NSBluetoothAlwaysUsageDescription before CoreBluetooth may be used; the
# bare executable carries its Info.plist as the __TEXT,__info_plist section.
if(APPLE)
    set(HIO_CLI_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
    configure_file(macos/Info.plist.in "${HIO_CLI_INFO_PLIST}" @ONLY)
    target_link_options(hio PRIVATE
        "LINKER:-sectcreate,__TEXT,__info_plist,${HIO_CLI_INFO_PLIST}")
    set_property(TARGET hio APPEND PROPERTY LINK_DEPENDS "${HIO_CLI_INFO_PLIST}")
endif()

if(HIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
