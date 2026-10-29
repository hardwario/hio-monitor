# libs/mcp - the MCP protocol library hwio_mcp (plan/features/0013-m6-mcp-core-cli.md, ADR-0150):
# JSON-RPC 2.0, both protocol dialects (2025-11-25 "legacy", 2026-07-28 "modern", ADR-0095), the
# tool / resource / prompt registry, ToolCall, argument validation, the stdio transport, the
# bounded HTTP/1.1 transport and the token store (ADR-0096, ADR-0102).
#
# Backend library: QtCore + QtNetwork + hwio::core only. Besides the ADR-0004 GUI-module rule, a
# configure-time check (hwio_restrict_own_links, cmake/HWioBackendRules.cmake) fails when hwio_mcp
# links any other hwio:: target (ADR-0150).

hwio_add_backend_library(hwio_mcp
    include/hwio/mcp/activity.h
    include/hwio/mcp/consent.h
    include/hwio/mcp/http_parser.h
    include/hwio/mcp/http_server.h
    include/hwio/mcp/json_rpc.h
    include/hwio/mcp/limits.h
    include/hwio/mcp/offload.h
    include/hwio/mcp/protocol.h
    include/hwio/mcp/registry.h
    include/hwio/mcp/reply_channel.h
    include/hwio/mcp/request_context.h
    include/hwio/mcp/schema.h
    include/hwio/mcp/server.h
    include/hwio/mcp/stdio_transport.h
    include/hwio/mcp/token.h
    include/hwio/mcp/tool_call.h
    src/consent.cpp
    src/dialect_legacy.cpp
    src/dialect_legacy.h
    src/dialect_modern.cpp
    src/dialect_modern.h
    src/http_connection.cpp
    src/http_connection.h
    src/http_parser.cpp
    src/http_server.cpp
    src/json_rpc.cpp
    src/offload.cpp
    src/registry.cpp
    src/schema.cpp
    src/server.cpp
    src/stdio_transport.cpp
    src/token.cpp
    src/tool_call.cpp)
add_library(hwio::mcp ALIAS hwio_mcp)
target_include_directories(hwio_mcp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(hwio_mcp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hwio_mcp PUBLIC hwio::core Qt6::Core Qt6::Network)
if(WIN32)
    # TokenStore: owner-only ACL on the token file (Win32 security API).
    target_compile_definitions(hwio_mcp PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(hwio_mcp PRIVATE advapi32)
endif()

# ADR-0150: the protocol core links no hwio:: target except hwio::core.
hwio_restrict_own_links(hwio_mcp ALLOW hwio::core)

if(HWIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(HWIO_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()
