#pragma once

// IReplyChannel - where McpServer::handle() sends the messages caused by one incoming message
// (plan/features/0013 "Protocol core": transport-neutral entry point).
//
// Stdio: one channel per transport, shared by all requests and persistent (complete() writes the
// response, the channel stays open). HTTP: one channel per request; complete() ends the response
// (JSON, or the SSE stream), notify() before it streams progress (SSE) when the client accepts it.
//
// Thread safety: owner thread only.

#include <QJsonObject>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace hio::mcp {

// HTTP status hint of the final message (ignored by stdio).
enum class ReplyStatus : std::uint8_t {
    Ok,                 // 200 (JSON or SSE)
    Accepted,           // 202, no body (notifications and responses from the client)
    BadRequest,         // 400
    NotFound,           // 404
    ServiceUnavailable, // 503 (session / stream limits)
};

struct ReplyMeta {
    ReplyStatus status = ReplyStatus::Ok;
    std::optional<std::string> sessionId; // Mcp-Session-Id of a legacy initialize response
};

enum class CloseReason : std::uint8_t {
    PeerClosed, // the client closed the stream or the connection
    SlowClient, // the send queue exceeded Limits::maxSendQueueBytes
    Shutdown,   // the transport is being destroyed
};

class IReplyChannel {
public:
    IReplyChannel() = default;
    IReplyChannel(const IReplyChannel&) = delete;
    IReplyChannel& operator=(const IReplyChannel&) = delete;
    IReplyChannel(IReplyChannel&&) = delete;
    IReplyChannel& operator=(IReplyChannel&&) = delete;
    virtual ~IReplyChannel() = default;

    // Whether notifications can reach the client before the final message (stdio: always; HTTP:
    // the request's Accept includes text/event-stream).
    [[nodiscard]] virtual bool canStream() const = 0;
    // Hint: answer as a stream now (a progress token was sent, or a long-lived stream). No-op when
    // streaming is not possible or already started.
    virtual void startStream() = 0;
    // A notification related to the request (progress) or of a long-lived stream. Dropped when
    // the channel cannot stream or is closed.
    virtual void notify(const QJsonObject& notification) = 0;
    // The final message: a response, or an empty object with ReplyStatus::Accepted. HTTP channels
    // ignore everything after it; stdio writes it and stays open.
    virtual void complete(const QJsonObject& response, const ReplyMeta& meta) = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;
    // Replaces the handler that is called once when the peer goes away before complete(). The
    // server sets it for pending requests and streams.
    virtual void setClosedHandler(std::function<void(CloseReason)> handler) = 0;
};

} // namespace hio::mcp
