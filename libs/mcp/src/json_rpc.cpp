#include "hio/mcp/json_rpc.h"

#include "hio/mcp/protocol.h"
#include "hio/mcp/request_context.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>
#include <QStringLiteral>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <expected>

namespace hio::mcp {

namespace {

// Largest integer a double represents exactly (JSON numbers are doubles in QJsonValue).
constexpr double maxExactInteger = 9007199254740991.0; // 2^53 - 1

std::unexpected<ParseFailure> failure(int code, const char* message,
                                      const QJsonValue& id = QJsonValue(QJsonValue::Undefined)) {
    return std::unexpected(ParseFailure{
        .error = RpcError{.code = code, .message = QString::fromLatin1(message)}, .id = id});
}

} // namespace

std::uint64_t nextConnectionId() noexcept {
    static std::atomic<std::uint64_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

bool isValidRequestId(const QJsonValue& id) {
    if (id.isString()) {
        return true;
    }
    if (!id.isDouble()) {
        return false;
    }
    const double value = id.toDouble();
    return std::isfinite(value) && std::trunc(value) == value &&
           std::fabs(value) <= maxExactInteger;
}

std::expected<Message, ParseFailure> parseMessage(QByteArrayView bytes) {
    QJsonParseError parseError{};
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray::fromRawData(bytes.data(), bytes.size()), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return failure(protocol::errc::parseError, "Parse error");
    }
    if (document.isArray()) {
        return failure(protocol::errc::invalidRequest, "Batches are not supported");
    }
    if (!document.isObject()) {
        return failure(protocol::errc::invalidRequest, "Invalid Request");
    }
    const QJsonObject object = document.object();

    const QJsonValue idValue = object.value(QStringLiteral("id"));
    const bool hasId = !idValue.isUndefined();
    const QJsonValue echoId =
        hasId && isValidRequestId(idValue) ? idValue : QJsonValue(QJsonValue::Undefined);

    if (object.value(QStringLiteral("jsonrpc")) != QJsonValue(QStringLiteral("2.0"))) {
        return failure(protocol::errc::invalidRequest, "Invalid Request: jsonrpc must be \"2.0\"",
                       echoId);
    }
    if (hasId && !isValidRequestId(idValue)) {
        return failure(protocol::errc::invalidRequest,
                       "Invalid Request: id must be a string or an integer");
    }

    Message message;
    message.id = echoId;
    const QJsonValue methodValue = object.value(QStringLiteral("method"));
    if (methodValue.isUndefined()) {
        // A response (result or error): this server sends no requests, so it is only acknowledged.
        if (hasId && (object.contains(QStringLiteral("result")) ||
                      object.contains(QStringLiteral("error")))) {
            message.kind = MessageKind::Response;
            return message;
        }
        return failure(protocol::errc::invalidRequest, "Invalid Request: method is missing",
                       echoId);
    }
    if (!methodValue.isString() || methodValue.toString().isEmpty()) {
        return failure(protocol::errc::invalidRequest, "Invalid Request: method must be a string",
                       echoId);
    }
    message.method = methodValue.toString();
    message.kind = hasId ? MessageKind::Request : MessageKind::Notification;

    const QJsonValue paramsValue = object.value(QStringLiteral("params"));
    if (!paramsValue.isUndefined()) {
        if (!paramsValue.isObject()) {
            return failure(protocol::errc::invalidParams,
                           "Invalid params: params must be an object", echoId);
        }
        message.params = paramsValue.toObject();
    }
    return message;
}

QJsonObject resultResponse(const QJsonValue& id, const QJsonObject& result) {
    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    response.insert(QStringLiteral("id"), id);
    response.insert(QStringLiteral("result"), result);
    return response;
}

QJsonObject errorResponse(const QJsonValue& id, const RpcError& error) {
    QJsonObject errorObject;
    errorObject.insert(QStringLiteral("code"), error.code);
    errorObject.insert(QStringLiteral("message"), error.message);
    if (!error.data.isUndefined()) {
        errorObject.insert(QStringLiteral("data"), error.data);
    }
    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    if (!id.isUndefined() && !id.isNull()) {
        response.insert(QStringLiteral("id"), id);
    }
    response.insert(QStringLiteral("error"), errorObject);
    return response;
}

QJsonObject notificationMessage(const QString& method, const QJsonObject& params) {
    QJsonObject notification;
    notification.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    notification.insert(QStringLiteral("method"), method);
    notification.insert(QStringLiteral("params"), params);
    return notification;
}

QByteArray serialize(const QJsonObject& message) {
    return QJsonDocument(message).toJson(QJsonDocument::Compact);
}

} // namespace hio::mcp
