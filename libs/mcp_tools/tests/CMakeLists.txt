# Unit tests of libs/mcp_tools (GoogleTest + GMock, ADR-0010; plan/features/0013 "Tests: Tools"):
# the tools against in-process FakeConsoleSessions and a GMock provider, golden JSON (golden/),
# resources and the prompt. With HWIO_TEST_OVERRIDES also against real sessions on the fake J-Link
# library and the fake BLE backend (jlink_tools_test, owner_thread_test). Limits are shrunk
# instead of a fake clock. Run under ASan/UBSan and TSan by the presets.

add_executable(hwio_mcp_tools_tests
    console_tools_test.cpp
    discovery_tools_test.cpp
    main.cpp
    resources_test.cpp
    tools_test_support.h)
# The fake J-Link and the fake BLE backend exist only with HWIO_TEST_OVERRIDES (ADR-0127).
if(HWIO_TEST_OVERRIDES)
    target_sources(hwio_mcp_tools_tests PRIVATE
        jlink_tools_test.cpp
        owner_thread_test.cpp
        session_provider.h)
    target_include_directories(hwio_mcp_tools_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/libs/hw/ble/src"
        "${PROJECT_SOURCE_DIR}/libs/firmware/tests")
    target_compile_definitions(hwio_mcp_tools_tests PRIVATE
        "HWIO_FAKE_JLINK_LIBRARY=\"$<TARGET_FILE:hwio_fake_jlink>\""
        "HWIO_FAKE_JLINK_BASIC_LIBRARY=\"$<TARGET_FILE:hwio_fake_jlink_basic>\""
        "HWIO_BLE_SCENARIOS_DIR=\"${PROJECT_SOURCE_DIR}/tests/ble_scenarios\"")
    add_dependencies(hwio_mcp_tools_tests hwio_fake_jlink hwio_fake_jlink_basic)
endif()
hwio_apply_target_defaults(hwio_mcp_tools_tests)
target_include_directories(hwio_mcp_tools_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/libs/mcp/tests")
target_link_libraries(hwio_mcp_tools_tests PRIVATE hwio::mcp_tools GTest::gmock Qt6::Network)
target_compile_definitions(hwio_mcp_tools_tests PRIVATE
    "HWIO_MCP_TOOLS_GOLDEN_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/golden\"")

# Under ThreadSanitizer the catalog test is left out like the libs/firmware catalog tests:
# QNetworkAccessManager's HTTP thread lives inside the uninstrumented Qt libraries, so TSan reports
# Qt's own posted events as races (plan/features/0003, review R11).
# The owner-thread stall test (OwnerThread.*) measures dispatch latency, so it runs alone
# (RUN_SERIAL): parallel test processes on the same machine would distort the measurement.
set(_hwio_mcp_tools_test_filter "-OwnerThread.*")
if("thread" IN_LIST HWIO_SANITIZERS)
    set(_hwio_mcp_tools_test_filter "-OwnerThread.*:JLinkTools.FlashFromTheCatalog")
endif()
gtest_discover_tests(hwio_mcp_tools_tests
    DISCOVERY_TIMEOUT 60
    TEST_FILTER "${_hwio_mcp_tools_test_filter}"
    PROPERTIES LABELS "unit")
if(HWIO_TEST_OVERRIDES)
    gtest_discover_tests(hwio_mcp_tools_tests
        DISCOVERY_TIMEOUT 60
        TEST_FILTER "OwnerThread.*"
        PROPERTIES LABELS "unit" RUN_SERIAL TRUE)
endif()
