#pragma once

// MCP protocol constants: revision strings, method names, JSON-RPC error numbers, _meta keys, HTTP
// headers and caching hints (plan/features/0013-m6-mcp-core-cli.md, ADR-0095, ADR-0096: all
// protocol strings live in this one header).
//
// Dialects (ADR-0095): "modern" = 2026-07-28 (stateless, per-request _meta), "legacy" =
// 2025-11-25 (initialize handshake, sessions). 2025-06-18 and 2025-03-26 are accepted in
// initialize and served with 2025-11-25 semantics.

#include <array>
#include <cstdint>
#include <string_view>

namespace hio::mcp::protocol {

inline constexpr std::string_view modernRevision = "2026-07-28";
inline constexpr std::string_view legacyRevision = "2025-11-25";
// Older legacy revisions accepted in initialize (echoed back, served as 2025-11-25).
inline constexpr std::array<std::string_view, 2> olderLegacyRevisions{"2025-06-18", "2025-03-26"};
// The list in UnsupportedProtocolVersion (-32022) and server/discover "supportedVersions".
inline constexpr std::array<std::string_view, 2> supportedRevisions{modernRevision, legacyRevision};

// Server identity (initialize "serverInfo", modern result _meta
// "io.modelcontextprotocol/serverInfo").
inline constexpr std::string_view serverName = "hio-monitor";

namespace method {
// Both dialects
inline constexpr std::string_view toolsList = "tools/list";
inline constexpr std::string_view toolsCall = "tools/call";
inline constexpr std::string_view resourcesList = "resources/list";
inline constexpr std::string_view resourcesTemplatesList = "resources/templates/list";
inline constexpr std::string_view resourcesRead = "resources/read";
inline constexpr std::string_view promptsList = "prompts/list";
inline constexpr std::string_view promptsGet = "prompts/get";
inline constexpr std::string_view cancelled = "notifications/cancelled";
inline constexpr std::string_view progress = "notifications/progress";
inline constexpr std::string_view resourcesUpdated = "notifications/resources/updated";
inline constexpr std::string_view resourcesListChanged = "notifications/resources/list_changed";
// Legacy only
inline constexpr std::string_view initialize = "initialize";
inline constexpr std::string_view initialized = "notifications/initialized";
inline constexpr std::string_view ping = "ping";
inline constexpr std::string_view resourcesSubscribe = "resources/subscribe";
inline constexpr std::string_view resourcesUnsubscribe = "resources/unsubscribe";
// Modern only
inline constexpr std::string_view serverDiscover = "server/discover";
inline constexpr std::string_view subscriptionsListen = "subscriptions/listen";
inline constexpr std::string_view subscriptionsAcknowledged =
    "notifications/subscriptions/acknowledged";
} // namespace method

namespace errc {
inline constexpr int parseError = -32700;
inline constexpr int invalidRequest = -32600;
inline constexpr int methodNotFound = -32601;
inline constexpr int invalidParams = -32602;
inline constexpr int internalError = -32603;
// Legacy (2025-11-25) "resource not found"; modern uses invalidParams.
inline constexpr int legacyResourceNotFound = -32002;
// Modern (2026-07-28)
inline constexpr int headerMismatch = -32020;
inline constexpr int missingRequiredClientCapability = -32021;
inline constexpr int unsupportedProtocolVersion = -32022;
} // namespace errc

namespace meta {
inline constexpr std::string_view key = "_meta";
inline constexpr std::string_view protocolVersion = "io.modelcontextprotocol/protocolVersion";
inline constexpr std::string_view clientInfo = "io.modelcontextprotocol/clientInfo";
inline constexpr std::string_view clientCapabilities = "io.modelcontextprotocol/clientCapabilities";
inline constexpr std::string_view serverInfo = "io.modelcontextprotocol/serverInfo";
inline constexpr std::string_view subscriptionId = "io.modelcontextprotocol/subscriptionId";
inline constexpr std::string_view progressToken = "progressToken";
} // namespace meta

// HTTP header names (compared case-insensitively).
namespace header {
inline constexpr std::string_view protocolVersion = "mcp-protocol-version";
inline constexpr std::string_view method = "mcp-method";
inline constexpr std::string_view name = "mcp-name";
inline constexpr std::string_view sessionId = "mcp-session-id";
// Sentinel of a Base64-encoded Mcp-Name / Mcp-Param-* value (2026-07-28).
inline constexpr std::string_view base64Prefix = "=?base64?";
inline constexpr std::string_view base64Suffix = "?=";
} // namespace header

// Caching hints of the modern dialect (2026-07-28 "CacheableResult"; decided by the user during the
// implementation of plan 0013: "private" everywhere).
namespace cache {
inline constexpr std::string_view scope = "private";
inline constexpr std::int64_t discoverTtlMs = 3'600'000;
inline constexpr std::int64_t toolsListTtlMs = 3'600'000;
inline constexpr std::int64_t promptsListTtlMs = 3'600'000;
inline constexpr std::int64_t resourceTemplatesListTtlMs = 3'600'000;
inline constexpr std::int64_t resourcesListTtlMs = 5'000;
inline constexpr std::int64_t resourcesReadTtlMs = 0;
} // namespace cache

// Modern result type of every result this server sends (no MRTR, ADR-0095).
inline constexpr std::string_view resultTypeComplete = "complete";

} // namespace hio::mcp::protocol
