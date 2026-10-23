# Unit and integration tests of libs/device (GoogleTest, ADR-0010). The DeviceSession tests use an
# in-process scripted IJLinkApi; the fake-library tests run the real locator, loader and library
# copies against tests/fake_jlink (phase E2). Run under ASan/UBSan and TSan by the presets.

add_executable(hio_device_tests
    ble_lock_test.cpp
    ble_session_test.cpp
    command_history_test.cpp
    completion_test.cpp
    console_file_sink_test.cpp
    console_log_test.cpp
    device_session_test.cpp
    fake_library_test.cpp
    line_assembler_test.cpp
    lock_holder.h
    main.cpp
    diagnose_session_test.cpp
    operation_session_test.cpp
    pib_test.cpp
    probe_lock_test.cpp
    profile_test.cpp
    scripted_api.h
    session_test_support.h
    target_session_test.cpp
    workflow_test.cpp)
# The fake BLE backend exists only with HIO_TEST_OVERRIDES (ADR-0127).
if(HIO_TEST_OVERRIDES)
    target_sources(hio_device_tests PRIVATE ble_session_fake_test.cpp)
    target_include_directories(hio_device_tests PRIVATE "${PROJECT_SOURCE_DIR}/libs/hw/ble/src")
endif()
hio_apply_target_defaults(hio_device_tests)
target_include_directories(hio_device_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${PROJECT_SOURCE_DIR}/libs/hw/jlink/tests"
    "${PROJECT_SOURCE_DIR}/libs/hw/ble/tests"
    "${PROJECT_SOURCE_DIR}/libs/firmware/tests")
target_link_libraries(hio_device_tests PRIVATE hio::device GTest::gmock)
target_compile_definitions(hio_device_tests PRIVATE
    "HIO_FAKE_JLINK_LIBRARY=\"$<TARGET_FILE:hio_fake_jlink>\""
    "HIO_FAKE_JLINK_BASIC_LIBRARY=\"$<TARGET_FILE:hio_fake_jlink_basic>\""
    "HIO_BLE_SCENARIOS_DIR=\"${PROJECT_SOURCE_DIR}/tests/ble_scenarios\"")
add_dependencies(hio_device_tests hio_fake_jlink hio_fake_jlink_basic)

gtest_discover_tests(hio_device_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit")
