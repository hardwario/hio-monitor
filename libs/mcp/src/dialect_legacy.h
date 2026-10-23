#pragma once

// Legacy dialect (2025-11-25) handshake rules for McpServer (internal; ADR-0095 as amended by
// ADR-0159): version negotiation, the initialize result and the client identity.

#include "dialect_modern.h"
#include "hio/mcp/request_context.h"

#include <QJsonObject>
#include <QString>

#include <string>

namespace hio::mcp::detail {

// The requested revision when it is 2025-11-25, 2025-06-18 or 2025-03-26 (echoed, served with
// 2025-11-25 semantics), else 2025-11-25 (ADR-0159, amending ADR-0095: the spec requires an
// answer with a version the server supports; the client disconnects when it cannot use it).
// Modern requests with an unknown version still get -32022 (dialect_modern.h).
[[nodiscard]] std::string negotiateLegacyVersion(const QString& requested);

// {"protocolVersion", "capabilities", "serverInfo", "instructions"?}
[[nodiscard]] QJsonObject initializeResult(const std::string& version,
                                           const ServerIdentity& identity);

// params.clientInfo of initialize (empty when absent).
[[nodiscard]] ClientInfo legacyClientInfo(const QJsonObject& params);

} // namespace hio::mcp::detail
