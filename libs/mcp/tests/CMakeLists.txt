# Unit tests of libs/mcp (GoogleTest, ADR-0010): JSON-RPC, schema validation, the registry, both
# protocol dialects with golden JSON exchanges (golden/), the stdio and HTTP transports (QTcpSocket
# test client), the HTTP parser, the token store and the Offloader (ADR-0162). Limits are shrunk
# instead of a fake clock. Run under ASan/UBSan and TSan. mcp_test_support.h holds reusable test
# doubles (also for libs/mcp_tools tests).

add_executable(hio_mcp_tests
    golden_test.cpp
    http_parser_test.cpp
    http_server_test.cpp
    json_rpc_test.cpp
    main.cpp
    mcp_test_support.h
    offload_test.cpp
    registry_test.cpp
    schema_test.cpp
    server_test.cpp
    stdio_transport_test.cpp
    token_test.cpp
    tool_call_test.cpp)
hio_apply_target_defaults(hio_mcp_tests)
target_include_directories(hio_mcp_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(hio_mcp_tests PRIVATE hio::mcp GTest::gmock Qt6::Network)
target_compile_definitions(hio_mcp_tests PRIVATE
    "HIO_MCP_GOLDEN_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/golden\"")

gtest_discover_tests(hio_mcp_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit")
