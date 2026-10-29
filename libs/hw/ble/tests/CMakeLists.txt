# Unit tests of libs/hw/ble (GoogleTest + GMock, ADR-0010, ADR-0087): NusLink on a GMock IBleApi,
# BleHost (also under TSan), and - in builds with HWIO_TEST_OVERRIDES (ADR-0127) - the fake backend
# and BleScanner with the shared scenarios in tests/ble_scenarios.

add_executable(hwio_hw_ble_tests
    ble_host_test.cpp
    controller_closer_test.cpp
    legacy_regression_test.cpp
    main.cpp
    mock_ble_api.h
    nus_link_test.cpp)
if(HWIO_TEST_OVERRIDES)
    target_sources(hwio_hw_ble_tests PRIVATE fake_ble_test.cpp)
endif()
hwio_apply_target_defaults(hwio_hw_ble_tests)
target_include_directories(hwio_hw_ble_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(hwio_hw_ble_tests PRIVATE hwio::hw_ble GTest::gmock)
target_compile_definitions(hwio_hw_ble_tests PRIVATE
    ${HWIO_TEST_OVERRIDES_DEFINE}
    "HWIO_BLE_SCENARIOS_DIR=\"${PROJECT_SOURCE_DIR}/tests/ble_scenarios\"")

gtest_discover_tests(hwio_hw_ble_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit")
