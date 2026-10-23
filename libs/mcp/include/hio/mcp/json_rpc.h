#pragma once

// JSON-RPC 2.0 messages of MCP (plan/features/0013 "Protocol core", ADR-0019, ADR-0096).
//
// Rules: single messages only (a batch is -32600, batches were removed in 2025-06-18); request ids
// are strings or integers (|id| <= 2^53) and are echoed verbatim; params, when present, are an
// object. Input size is bounded by the transports (stdio line 4 MiB, HTTP body 4 MiB).
//
// Note for the conformance run (plan 0013 E1, review R7): an invalid message is answered with an
// error even when it was meant as a notification (the kind cannot be known when it does not
// parse; JSON-RPC 2.0 allows the answer with id null), and integer ids beyond 2^53 are rejected
// (-32600) because QJsonValue keeps numbers as doubles and could not echo them verbatim.

#include <QByteArray>
#include <QByteArrayView>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <cstdint>
#include <expected>

namespace hio::mcp {

// A JSON-RPC error object. `data` is omitted when undefined.
struct RpcError {
    int code = 0;
    QString message;
    QJsonValue data = QJsonValue(QJsonValue::Undefined);
};

enum class MessageKind : std::uint8_t {
    Request,      // method + id
    Notification, // method, no id
    Response,     // result or error + id (clients never need to send one to this server)
};

struct Message {
    MessageKind kind = MessageKind::Request;
    QJsonValue id = QJsonValue(QJsonValue::Undefined); // string or integer for requests/responses
    QString method;
    QJsonObject params; // empty when absent
};

// A message that cannot be processed. `id` is the request id when it could be read (it is echoed
// in the error response), else undefined.
struct ParseFailure {
    RpcError error;
    QJsonValue id = QJsonValue(QJsonValue::Undefined);
};

// Parses one message: -32700 for invalid JSON, -32600 for batches and malformed envelopes,
// -32602 for params that are not an object.
[[nodiscard]] std::expected<Message, ParseFailure> parseMessage(QByteArrayView bytes);

// A JSON-RPC id is a string or an integer within the exact double range.
[[nodiscard]] bool isValidRequestId(const QJsonValue& id);

[[nodiscard]] QJsonObject resultResponse(const QJsonValue& id, const QJsonObject& result);
// The "id" member is left out when `id` is undefined or null (the MCP schemas allow no null id).
[[nodiscard]] QJsonObject errorResponse(const QJsonValue& id, const RpcError& error);
[[nodiscard]] QJsonObject notificationMessage(const QString& method, const QJsonObject& params);

// Compact UTF-8 serialization without a trailing newline (one message per stdio line).
[[nodiscard]] QByteArray serialize(const QJsonObject& message);

} // namespace hio::mcp
