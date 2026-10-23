#include "dialect_modern.h"

#include "hio/mcp/json_rpc.h"
#include "hio/mcp/protocol.h"
#include "hio/mcp/reply_channel.h"
#include "hio/mcp/request_context.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QString>
#include <QStringLiteral>
#include <QtTypes>

#include <cstdint>
#include <optional>
#include <string_view>

namespace hio::mcp::detail {

namespace {

QString latin1(std::string_view text) {
    return QLatin1StringView(text.data(), static_cast<qsizetype>(text.size())).toString();
}

DialectError headerMismatch(const QString& message) {
    return DialectError{.error = RpcError{.code = protocol::errc::headerMismatch,
                                          .message = QStringLiteral("Header mismatch: ") + message},
                        .status = ReplyStatus::BadRequest};
}

bool needsNameHeader(const QString& method) {
    return method == latin1(protocol::method::toolsCall) ||
           method == latin1(protocol::method::resourcesRead) ||
           method == latin1(protocol::method::promptsGet);
}

} // namespace

QJsonObject serverCapabilities() {
    QJsonObject resources;
    resources.insert(QStringLiteral("subscribe"), true);
    resources.insert(QStringLiteral("listChanged"), true);
    QJsonObject capabilities;
    capabilities.insert(QStringLiteral("tools"), QJsonObject());
    capabilities.insert(QStringLiteral("resources"), resources);
    capabilities.insert(QStringLiteral("prompts"), QJsonObject());
    return capabilities;
}

QJsonObject serverInfo(const ServerIdentity& identity) {
    QJsonObject info;
    info.insert(QStringLiteral("name"), identity.name);
    info.insert(QStringLiteral("version"), identity.version);
    return info;
}

std::optional<QString> modernVersion(const QJsonObject& params) {
    const QJsonValue value = params.value(latin1(protocol::meta::key))
                                 .toObject()
                                 .value(latin1(protocol::meta::protocolVersion));
    if (!value.isString()) {
        return std::nullopt;
    }
    return value.toString();
}

DialectError missingMeta(std::string_view field) {
    QString pointer = QStringLiteral("/_meta/") + latin1(field);
    pointer.replace(QStringLiteral("io.modelcontextprotocol/"),
                    QStringLiteral("io.modelcontextprotocol~1"));
    QJsonObject data;
    data.insert(QStringLiteral("pointer"), pointer);
    return DialectError{
        .error =
            RpcError{.code = protocol::errc::invalidParams,
                     .message = QStringLiteral("Invalid params: _meta %1 is missing or invalid")
                                    .arg(latin1(field)),
                     .data = data},
        .status = ReplyStatus::BadRequest};
}

DialectError unsupportedVersion(const QString& requested) {
    QJsonArray supported;
    for (const std::string_view revision : protocol::supportedRevisions) {
        supported.append(latin1(revision));
    }
    QJsonObject data;
    data.insert(QStringLiteral("supported"), supported);
    data.insert(QStringLiteral("requested"), requested);
    return DialectError{.error = RpcError{.code = protocol::errc::unsupportedProtocolVersion,
                                          .message = QStringLiteral("Unsupported protocol version"),
                                          .data = data},
                        .status = ReplyStatus::BadRequest};
}

std::optional<DialectError>
checkModernHeaders(const Message& message, const TransportInfo& transport, const QString& version) {
    if (transport.kind != TransportKind::Http || message.kind != MessageKind::Request) {
        return std::nullopt;
    }
    if (!transport.protocolVersionHeader) {
        return headerMismatch(QStringLiteral("MCP-Protocol-Version header is missing"));
    }
    if (QString::fromLatin1(*transport.protocolVersionHeader) != version) {
        return headerMismatch(
            QStringLiteral("MCP-Protocol-Version header does not match the body"));
    }
    if (!transport.methodHeader) {
        return headerMismatch(QStringLiteral("Mcp-Method header is missing"));
    }
    if (QString::fromLatin1(*transport.methodHeader) != message.method) {
        return headerMismatch(QStringLiteral("Mcp-Method header does not match the body"));
    }
    if (needsNameHeader(message.method)) {
        if (!transport.nameHeader) {
            return headerMismatch(QStringLiteral("Mcp-Name header is missing"));
        }
        const auto name = decodeHeaderValue(*transport.nameHeader);
        if (!name) {
            return headerMismatch(QStringLiteral("Mcp-Name header has invalid characters"));
        }
        const QString bodyKey = message.method == latin1(protocol::method::resourcesRead)
                                    ? QStringLiteral("uri")
                                    : QStringLiteral("name");
        if (message.params.value(bodyKey).toString() != *name) {
            return headerMismatch(QStringLiteral("Mcp-Name header does not match the body"));
        }
    }
    return std::nullopt;
}

ClientInfo modernClientInfo(const QJsonObject& params) {
    const QJsonObject info = params.value(latin1(protocol::meta::key))
                                 .toObject()
                                 .value(latin1(protocol::meta::clientInfo))
                                 .toObject();
    return ClientInfo{.name = info.value(QStringLiteral("name")).toString().toStdString(),
                      .version = info.value(QStringLiteral("version")).toString().toStdString()};
}

QJsonObject decorateModernResult(QJsonObject result, const ServerIdentity& identity,
                                 std::optional<std::int64_t> ttlMs) {
    result.insert(QStringLiteral("resultType"), latin1(protocol::resultTypeComplete));
    QJsonObject meta = result.value(latin1(protocol::meta::key)).toObject();
    meta.insert(latin1(protocol::meta::serverInfo), serverInfo(identity));
    result.insert(latin1(protocol::meta::key), meta);
    if (ttlMs) {
        result.insert(QStringLiteral("ttlMs"), static_cast<qint64>(*ttlMs));
        result.insert(QStringLiteral("cacheScope"), latin1(protocol::cache::scope));
    }
    return result;
}

std::optional<QString> decodeHeaderValue(std::string_view raw) {
    const std::string_view prefix = protocol::header::base64Prefix;
    const std::string_view suffix = protocol::header::base64Suffix;
    if (raw.size() >= prefix.size() + suffix.size() && raw.starts_with(prefix) &&
        raw.ends_with(suffix)) {
        const std::string_view encoded =
            raw.substr(prefix.size(), raw.size() - prefix.size() - suffix.size());
        const auto decoded = QByteArray::fromBase64Encoding(
            QByteArray(encoded.data(), static_cast<qsizetype>(encoded.size())),
            QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) {
            return std::nullopt;
        }
        return QString::fromUtf8(decoded.decoded);
    }
    for (const char c : raw) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte != '\t' && (byte < 0x20 || byte > 0x7E)) {
            return std::nullopt;
        }
    }
    return latin1(raw);
}

} // namespace hio::mcp::detail
