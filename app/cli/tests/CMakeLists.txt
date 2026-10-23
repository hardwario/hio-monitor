# Unit and end-to-end tests of the `hio` CLI.

add_executable(hio_cli_tests
    cli_app_test.cpp
    cli_process.h
    command_response_test.cpp
    console_events_test.cpp
    console_view_test.cpp
    diag_e2e_test.cpp
    e2e_test.cpp
    exit_codes_test.cpp
    flash_commands_test.cpp
    flash_e2e_test.cpp
    jlink_e2e_test.cpp
    main.cpp
    mcp_command_test.cpp
    mcp_e2e_test.cpp
    output_test.cpp
    target_commands_test.cpp
    target_e2e_test.cpp
    terminal_test.cpp)
# `hio ble` end-to-end tests need the fake BLE backend (ADR-0127).
if(HIO_TEST_OVERRIDES)
    target_sources(hio_cli_tests PRIVATE ble_e2e_test.cpp)
endif()
hio_apply_target_defaults(hio_cli_tests)
target_link_libraries(hio_cli_tests PRIVATE hio_cli_impl hio::miniz Qt6::Network GTest::gmock)
# Test builders shared with libs/firmware (HEX text, zips, the local HTTP catalog fixture).
target_include_directories(hio_cli_tests PRIVATE "${PROJECT_SOURCE_DIR}/libs/firmware/tests")
# The end-to-end tests run the real executable, the M1 ones with the fake J-Link library.
target_compile_definitions(hio_cli_tests PRIVATE
    "HIO_CLI_EXECUTABLE=\"$<TARGET_FILE:hio>\""
    "HIO_FAKE_JLINK_LIBRARY=\"$<TARGET_FILE:hio_fake_jlink>\""
    "HIO_FAKE_JLINK_BASIC_LIBRARY=\"$<TARGET_FILE:hio_fake_jlink_basic>\""
    "HIO_FAKE_NRFUTIL=\"$<TARGET_FILE:hio_fake_nrfutil>\""
    "HIO_BLE_SCENARIOS_DIR=\"${PROJECT_SOURCE_DIR}/tests/ble_scenarios\"")
add_dependencies(hio_cli_tests hio hio_fake_jlink hio_fake_jlink_basic hio_fake_nrfutil)
target_compile_definitions(hio_cli_tests PRIVATE ${HIO_TEST_OVERRIDES_DEFINE})

# Under ThreadSanitizer the two catalog downloads are left out (QNetworkAccessManager's HTTP thread
# is uninstrumented Qt code, see libs/firmware/tests/CMakeLists.txt).
set(_hio_cli_test_filter "")
if("thread" IN_LIST HIO_SANITIZERS)
    set(_hio_cli_test_filter TEST_FILTER "-FlashCli.FlashByCatalogId:FlashCli.CatalogNetworkDown:FlashCli.LteFlashDesignatedFirmware:FlashCli.CatalogInfoAndVerifiedIdFlash")
endif()
gtest_discover_tests(hio_cli_tests
    DISCOVERY_TIMEOUT 60
    ${_hio_cli_test_filter}
    PROPERTIES LABELS "unit")
