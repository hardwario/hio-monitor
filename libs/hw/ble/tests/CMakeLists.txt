# Unit tests of libs/hw/ble (GoogleTest + GMock, ADR-0010, ADR-0087): NusLink on a GMock IBleApi,
# BleHost (also under TSan), and - in builds with HIO_TEST_OVERRIDES (ADR-0127) - the fake backend
# and BleScanner with the shared scenarios in tests/ble_scenarios.

add_executable(hio_hw_ble_tests
    ble_host_test.cpp
    legacy_regression_test.cpp
    main.cpp
    mock_ble_api.h
    nus_link_test.cpp)
if(HIO_TEST_OVERRIDES)
    target_sources(hio_hw_ble_tests PRIVATE fake_ble_test.cpp)
endif()
hio_apply_target_defaults(hio_hw_ble_tests)
target_include_directories(hio_hw_ble_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(hio_hw_ble_tests PRIVATE hio::hw_ble GTest::gmock)
target_compile_definitions(hio_hw_ble_tests PRIVATE
    ${HIO_TEST_OVERRIDES_DEFINE}
    "HIO_BLE_SCENARIOS_DIR=\"${PROJECT_SOURCE_DIR}/tests/ble_scenarios\"")

gtest_discover_tests(hio_hw_ble_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit")
