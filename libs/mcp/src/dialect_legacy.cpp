#include "dialect_legacy.h"

#include "dialect_modern.h"
#include "hio/mcp/protocol.h"
#include "hio/mcp/request_context.h"

#include <QJsonObject>
#include <QString>
#include <QStringLiteral>

#include <string>
#include <string_view>

namespace hio::mcp::detail {

std::string negotiateLegacyVersion(const QString& requested) {
    std::string text = requested.toStdString();
    if (text == protocol::legacyRevision) {
        return text;
    }
    for (const std::string_view older : protocol::olderLegacyRevisions) {
        if (text == older) {
            return text;
        }
    }
    return std::string(protocol::legacyRevision);
}

QJsonObject initializeResult(const std::string& version, const ServerIdentity& identity) {
    QJsonObject result;
    result.insert(QStringLiteral("protocolVersion"), QString::fromStdString(version));
    result.insert(QStringLiteral("capabilities"), serverCapabilities());
    result.insert(QStringLiteral("serverInfo"), serverInfo(identity));
    if (!identity.instructions.isEmpty()) {
        result.insert(QStringLiteral("instructions"), identity.instructions);
    }
    return result;
}

ClientInfo legacyClientInfo(const QJsonObject& params) {
    const QJsonObject info = params.value(QStringLiteral("clientInfo")).toObject();
    return ClientInfo{.name = info.value(QStringLiteral("name")).toString().toStdString(),
                      .version = info.value(QStringLiteral("version")).toString().toStdString()};
}

} // namespace hio::mcp::detail
