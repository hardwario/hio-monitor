# Unit and end-to-end tests of the `hwio` CLI.

add_executable(hwio_cli_tests
    cli_app_test.cpp
    cli_process.h
    command_response_test.cpp
    console_events_test.cpp
    console_learning_test.cpp
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
    terminal_test.cpp
    update_e2e_test.cpp)
# `hwio ble` end-to-end tests need the fake BLE backend (ADR-0127).
if(HWIO_TEST_OVERRIDES)
    target_sources(hwio_cli_tests PRIVATE ble_e2e_test.cpp)
endif()
hwio_apply_target_defaults(hwio_cli_tests)
target_link_libraries(hwio_cli_tests PRIVATE hwio_cli_impl hwio::miniz Qt6::Network GTest::gmock)
# Test builders shared with libs/firmware (HEX text, zips, the local HTTP catalog fixture).
target_include_directories(hwio_cli_tests PRIVATE "${PROJECT_SOURCE_DIR}/libs/firmware/tests")
# The end-to-end tests run the real executable, the M1 ones with the fake J-Link library.
target_compile_definitions(hwio_cli_tests PRIVATE
    "HWIO_CLI_EXECUTABLE=\"$<TARGET_FILE:hwio>\""
    "HWIO_FAKE_JLINK_LIBRARY=\"$<TARGET_FILE:hwio_fake_jlink>\""
    "HWIO_FAKE_JLINK_BASIC_LIBRARY=\"$<TARGET_FILE:hwio_fake_jlink_basic>\""
    "HWIO_FAKE_NRFUTIL=\"$<TARGET_FILE:hwio_fake_nrfutil>\""
    "HWIO_BLE_SCENARIOS_DIR=\"${PROJECT_SOURCE_DIR}/tests/ble_scenarios\"")
add_dependencies(hwio_cli_tests hwio hwio_fake_jlink hwio_fake_jlink_basic hwio_fake_nrfutil)
target_compile_definitions(hwio_cli_tests PRIVATE ${HWIO_TEST_OVERRIDES_DEFINE})

# Under ThreadSanitizer the catalog downloads and the update checks (plan 0016) are left out (QNetworkAccessManager's HTTP thread
# is uninstrumented Qt code, see libs/firmware/tests/CMakeLists.txt).
set(_hwio_cli_test_filter "")
if("thread" IN_LIST HWIO_SANITIZERS)
    set(_hwio_cli_test_filter TEST_FILTER "-FlashCli.FlashByCatalogId:FlashCli.CatalogNetworkDown:FlashCli.LteFlashDesignatedFirmware:FlashCli.CatalogInfoAndVerifiedIdFlash:UpdateCli.*")
endif()
gtest_discover_tests(hwio_cli_tests
    DISCOVERY_TIMEOUT 60
    ${_hwio_cli_test_filter}
    PROPERTIES LABELS "unit")
