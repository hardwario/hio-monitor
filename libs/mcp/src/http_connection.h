#pragma once

// One HTTP/1.1 connection of HttpServer (internal; plan/features/0013 "HTTP transport",
// ADR-0102): the incremental parser, the request checks in the documented order, JSON or SSE
// responses through HttpReplyChannel, keep-alive, timeouts and the slow-client limit.
// One request at a time per connection (pipelined requests wait in the bounded socket buffer).
//
// Thread safety: owner thread only.

#include "hio/mcp/http_parser.h"
#include "hio/mcp/limits.h"
#include "hio/mcp/reply_channel.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QtTypes>

#include <cstdint>
#include <deque>
#include <memory>
#include <string>

class QTcpSocket;

namespace hio::mcp {
class McpServer;
}

namespace hio::mcp::detail {

class HttpReplyChannel;

// Failed-token bookkeeping shared by all connections (ADR-0102: 10 failures in 60 s -> 429 for
// 10 s for everybody).
class AuthGuard {
public:
    explicit AuthGuard(const Limits& limits);
    [[nodiscard]] bool locked();
    void recordFailure();

private:
    const Limits* m_limits; // non-owning, never null (the server's limits)
    QElapsedTimer m_clock;
    std::deque<qint64> m_failures; // at most Limits::authFailureLimit entries
    qint64 m_lockedAt = -1;
};

// Owned by HttpServer; the connections keep a non-owning pointer to it.
struct HttpShared {
    McpServer* server = nullptr;    // non-owning, never null
    const Limits* limits = nullptr; // non-owning, never null (McpServer::limits())
    std::string token;
    quint16 port = 0;
    AuthGuard auth;
};

class HttpConnection : public QObject {
    Q_OBJECT

public:
    // Takes `socket` over as a Qt child.
    HttpConnection(QTcpSocket& socket, HttpShared& shared);
    ~HttpConnection() override;
    HttpConnection(const HttpConnection&) = delete;
    HttpConnection& operator=(const HttpConnection&) = delete;
    HttpConnection(HttpConnection&&) = delete;
    HttpConnection& operator=(HttpConnection&&) = delete;

    void shutdown();
    [[nodiscard]] bool isClosed() const noexcept { return m_phase == Phase::Closed; }

    // HttpReplyChannel API; `serial` identifies the request, stale calls are ignored.
    [[nodiscard]] bool isCurrent(std::uint64_t serial) const noexcept;
    void beginStream(std::uint64_t serial);
    void sendEvent(std::uint64_t serial, const QByteArray& json);
    void finishResponse(std::uint64_t serial, const QJsonObject& response, const ReplyMeta& meta);

signals:
    void closed();

private:
    enum class Phase : std::uint8_t {
        Reading,   // waiting for / reading a request
        Handling,  // request dispatched, no response bytes yet
        Streaming, // SSE response in progress
        Closing,   // refusal sent, closing after the write
        Closed,
    };

    void onReadyRead();
    [[nodiscard]] bool checkRequest();
    [[nodiscard]] bool checkShape();
    void dispatch();
    void afterResponse();
    void refuse(int status, const QByteArray& reason, const QByteArray& extraHeaders = {});
    void refuseParse();
    void sendSimple(int status, const QByteArray& extraHeaders = {});
    void writeRaw(const QByteArray& bytes);
    void writeChunk(const QByteArray& data);
    void closeConnection(CloseReason reason);

    QTcpSocket& m_socket;
    HttpShared* m_shared; // non-owning, outlives the connection
    HttpParser m_parser;
    QByteArray m_inbox;
    Phase m_phase = Phase::Reading;
    std::uint64_t m_connectionId;
    std::uint64_t m_serial = 0;
    std::weak_ptr<HttpReplyChannel> m_active;
    QTimer m_headerTimer;
    QTimer m_bodyTimer;
    QTimer m_idleTimer;
    QTimer m_jsonTimer;
    QTimer m_heartbeatTimer;
    QTimer m_closingTimer; // Phase::Closing -> abort after Limits::closingTimeout
};

} // namespace hio::mcp::detail
