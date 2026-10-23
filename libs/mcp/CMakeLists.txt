# libs/mcp - the MCP protocol library hio_mcp (plan/features/0013-m6-mcp-core-cli.md, ADR-0150):
# JSON-RPC 2.0, both protocol dialects (2025-11-25 "legacy", 2026-07-28 "modern", ADR-0095), the
# tool / resource / prompt registry, ToolCall, argument validation, the stdio transport, the
# bounded HTTP/1.1 transport and the token store (ADR-0096, ADR-0102).
#
# Backend library: QtCore + QtNetwork + hio::core only. Besides the ADR-0004 GUI-module rule, a
# configure-time check (hio_restrict_own_links, cmake/HioBackendRules.cmake) fails when hio_mcp
# links any other hio:: target (ADR-0150).

hio_add_backend_library(hio_mcp
    include/hio/mcp/activity.h
    include/hio/mcp/consent.h
    include/hio/mcp/http_parser.h
    include/hio/mcp/http_server.h
    include/hio/mcp/json_rpc.h
    include/hio/mcp/limits.h
    include/hio/mcp/offload.h
    include/hio/mcp/protocol.h
    include/hio/mcp/registry.h
    include/hio/mcp/reply_channel.h
    include/hio/mcp/request_context.h
    include/hio/mcp/schema.h
    include/hio/mcp/server.h
    include/hio/mcp/stdio_transport.h
    include/hio/mcp/token.h
    include/hio/mcp/tool_call.h
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
add_library(hio::mcp ALIAS hio_mcp)
target_include_directories(hio_mcp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(hio_mcp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hio_mcp PUBLIC hio::core Qt6::Core Qt6::Network)
if(WIN32)
    # TokenStore: owner-only ACL on the token file (Win32 security API).
    target_compile_definitions(hio_mcp PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(hio_mcp PRIVATE advapi32)
endif()

# ADR-0150: the protocol core links no hio:: target except hio::core.
hio_restrict_own_links(hio_mcp ALLOW hio::core)

if(HIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(HIO_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()
