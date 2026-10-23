#pragma once

// Modern dialect (2026-07-28) envelope rules for McpServer (internal; ADR-0095): per-request _meta,
// HTTP header/body validation, result decoration (resultType, serverInfo, caching hints) and the
// shared server description (capabilities, identity).

#include "hio/mcp/json_rpc.h"
#include "hio/mcp/reply_channel.h"
#include "hio/mcp/request_context.h"

#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <optional>
#include <string_view>

namespace hio::mcp::detail {

struct ServerIdentity {
    QString name;
    QString version;
    QString instructions;
};

// A protocol error with the HTTP status hint of its response.
struct DialectError {
    RpcError error;
    ReplyStatus status = ReplyStatus::Ok;
};

// Server capabilities, the same for both dialects: tools, resources (subscribe + listChanged),
// prompts.
[[nodiscard]] QJsonObject serverCapabilities();
[[nodiscard]] QJsonObject serverInfo(const ServerIdentity& identity);

// _meta["io.modelcontextprotocol/protocolVersion"] (nullopt when missing or not a string).
[[nodiscard]] std::optional<QString> modernVersion(const QJsonObject& params);
// -32602 / 400 for a request without the required _meta fields; `field` is the missing key.
[[nodiscard]] DialectError missingMeta(std::string_view field);
// -32022 / 400 with data {"supported": [...], "requested": ...}.
[[nodiscard]] DialectError unsupportedVersion(const QString& requested);
// MCP-Protocol-Version, Mcp-Method and (tools/call, resources/read, prompts/get) Mcp-Name must be
// present and equal to the body (Mcp-Name after decoding the Base64 sentinel); -32020 / 400.
[[nodiscard]] std::optional<DialectError>
checkModernHeaders(const Message& message, const TransportInfo& transport, const QString& version);
// clientInfo from _meta (empty when absent).
[[nodiscard]] ClientInfo modernClientInfo(const QJsonObject& params);

// Adds resultType "complete", _meta serverInfo (kept: other _meta keys) and, when `ttlMs` is set,
// ttlMs + cacheScope.
[[nodiscard]] QJsonObject decorateModernResult(QJsonObject result, const ServerIdentity& identity,
                                               std::optional<std::int64_t> ttlMs);

// Decodes an Mcp-Name / Mcp-Param value: "=?base64?...?=" -> UTF-8 text; plain values must be
// visible ASCII, space or tab. nullopt for invalid values.
[[nodiscard]] std::optional<QString> decodeHeaderValue(std::string_view raw);

} // namespace hio::mcp::detail
