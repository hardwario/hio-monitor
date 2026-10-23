#pragma once

// Per-request context of the MCP server (plan/features/0013 "Protocol core"): the dialect, the
// client's identity and the key consent and activity use, plus what the transport tells the
// server about a message.

#include <QJsonValue>

#include <cstdint>
#include <optional>
#include <string>

namespace hio::mcp {

enum class Dialect : std::uint8_t {
    Legacy, // 2025-11-25 (and the older 2025-06-18 / 2025-03-26 wire)
    Modern, // 2026-07-28
};

struct ClientInfo {
    std::string name;
    std::string version;
};

struct RequestContext {
    Dialect dialect = Dialect::Modern;
    std::string protocolVersion; // negotiated (legacy) or requested (modern) revision
    ClientInfo client;
    // Legacy: the session id (also for stdio, where the server mints one per connection).
    // Modern: clientInfo.name ("unknown" when the client sent none).
    std::string clientKey;
    std::string tool;     // tools/call: the tool name; empty otherwise
    QJsonValue requestId; // as received (string or integer)
    std::optional<QJsonValue> progressToken;
};

enum class TransportKind : std::uint8_t {
    Stdio,
    Http,
};

// What the transport passes with every message. Header values are the raw (Latin-1) bytes; absent
// headers are nullopt.
struct TransportInfo {
    TransportKind kind = TransportKind::Stdio;
    // Unique per stdio transport / TCP connection (nextConnectionId()).
    std::uint64_t connectionId = 0;
    std::optional<std::string> protocolVersionHeader; // MCP-Protocol-Version
    std::optional<std::string> methodHeader;          // Mcp-Method
    std::optional<std::string> nameHeader;            // Mcp-Name
    std::optional<std::string> sessionId;             // Mcp-Session-Id
};

// A process-wide unique connection id (thread-safe; never 0).
[[nodiscard]] std::uint64_t nextConnectionId() noexcept;

} // namespace hio::mcp
