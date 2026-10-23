#include "http_connection.h"

#include "hio/core/log.h"
#include "hio/mcp/http_parser.h"
#include "hio/mcp/json_rpc.h"
#include "hio/mcp/limits.h"
#include "hio/mcp/protocol.h"
#include "hio/mcp/reply_channel.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/server.h"
#include "hio/mcp/token.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QByteArrayList>
#include <QByteArrayView>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringLiteral>
#include <QTcpSocket>
#include <QTimer>
#include <QtContainerFwd>
#include <QtCore/qtmetamacros.h>
#include <QtTypes>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace hio::mcp::detail {

namespace {

// Bytes read from the socket per step; the socket's read buffer is limited to this size too, so a
// client cannot make the server buffer more than one step ahead (TCP back-pressure).
constexpr qint64 readChunk = qint64{64} * 1024;

QByteArray statusLine(int status) {
    const char* text = "Error";
    switch (status) {
    case 200:
        text = "OK";
        break;
    case 202:
        text = "Accepted";
        break;
    case 400:
        text = "Bad Request";
        break;
    case 401:
        text = "Unauthorized";
        break;
    case 403:
        text = "Forbidden";
        break;
    case 404:
        text = "Not Found";
        break;
    case 405:
        text = "Method Not Allowed";
        break;
    case 406:
        text = "Not Acceptable";
        break;
    case 408:
        text = "Request Timeout";
        break;
    case 411:
        text = "Length Required";
        break;
    case 413:
        text = "Content Too Large";
        break;
    case 415:
        text = "Unsupported Media Type";
        break;
    case 421:
        text = "Misdirected Request";
        break;
    case 429:
        text = "Too Many Requests";
        break;
    case 431:
        text = "Request Header Fields Too Large";
        break;
    case 503:
        text = "Service Unavailable";
        break;
    case 505:
        text = "HTTP Version Not Supported";
        break;
    default:
        break;
    }
    return "HTTP/1.1 " + QByteArray::number(status) + ' ' + text + "\r\n";
}

int httpStatus(ReplyStatus status) {
    switch (status) {
    case ReplyStatus::Ok:
        return 200;
    case ReplyStatus::Accepted:
        return 202;
    case ReplyStatus::BadRequest:
        return 400;
    case ReplyStatus::NotFound:
        return 404;
    case ReplyStatus::ServiceUnavailable:
        return 503;
    }
    return 500;
}

// The media types of an Accept / Content-Type value, lower case, without parameters.
QByteArrayList mediaTypes(const QByteArray& value) {
    QByteArrayList types;
    for (const QByteArray& part : value.split(',')) {
        const qsizetype semicolon = part.indexOf(';');
        types.append((semicolon < 0 ? part : part.left(semicolon)).trimmed().toLower());
    }
    return types;
}

std::optional<std::string> headerString(const HttpRequest& request, QByteArrayView name) {
    if (auto value = request.header(name)) {
        return value->toStdString();
    }
    return std::nullopt;
}

} // namespace

// ---- AuthGuard ----------------------------------------------------------------------------------

AuthGuard::AuthGuard(const Limits& limits) : m_limits(&limits) { m_clock.start(); }

bool AuthGuard::locked() {
    if (m_lockedAt < 0) {
        return false;
    }
    if (m_clock.elapsed() - m_lockedAt < m_limits->authLockout.count()) {
        return true;
    }
    m_lockedAt = -1;
    return false;
}

void AuthGuard::recordFailure() {
    const qint64 now = m_clock.elapsed();
    while (!m_failures.empty() && now - m_failures.front() >= m_limits->authFailureWindow.count()) {
        m_failures.pop_front();
    }
    m_failures.push_back(now);
    if (m_failures.size() >= m_limits->authFailureLimit) {
        m_failures.clear();
        m_lockedAt = now;
        qCWarning(hio::log::lcMcp) << "MCP HTTP:" << m_limits->authFailureLimit
                                   << "failed authentications; every request is refused for"
                                   << m_limits->authLockout.count() << "ms";
    }
}

// ---- HttpReplyChannel ---------------------------------------------------------------------------

class HttpReplyChannel final : public IReplyChannel {
public:
    HttpReplyChannel(HttpConnection& connection, std::uint64_t serial, bool canStream)
        : m_connection(&connection), m_serial(serial), m_canStream(canStream) {}

    [[nodiscard]] bool canStream() const override { return m_canStream; }
    void startStream() override {
        if (!m_done && m_canStream && !m_connection.isNull()) {
            m_connection->beginStream(m_serial);
        }
    }
    void notify(const QJsonObject& notification) override {
        if (m_done || !m_canStream || m_connection.isNull()) {
            return;
        }
        m_connection->beginStream(m_serial);
        m_connection->sendEvent(m_serial, serialize(notification));
    }
    void complete(const QJsonObject& response, const ReplyMeta& meta) override {
        if (m_done) {
            return;
        }
        m_done = true;
        if (!m_connection.isNull()) {
            m_connection->finishResponse(m_serial, response, meta);
        }
    }
    [[nodiscard]] bool isOpen() const override {
        return !m_done && !m_closed && !m_connection.isNull() && m_connection->isCurrent(m_serial);
    }
    void setClosedHandler(std::function<void(CloseReason)> handler) override {
        m_handler = std::move(handler);
    }

    void peerClosed(CloseReason reason) {
        if (m_done || m_closed) {
            return;
        }
        m_closed = true;
        if (auto handler = std::exchange(m_handler, {})) {
            handler(reason);
        }
    }

private:
    QPointer<HttpConnection> m_connection;
    std::uint64_t m_serial;
    bool m_canStream;
    bool m_done = false;
    bool m_closed = false;
    std::function<void(CloseReason)> m_handler;
};

// ---- HttpConnection -----------------------------------------------------------------------------

HttpConnection::HttpConnection(QTcpSocket& socket, HttpShared& shared)
    : m_socket(socket), m_shared(&shared),
      m_parser(HttpLimits{.maxHeaderBytes = shared.limits->maxHeaderBytes,
                          .maxHeaderCount = shared.limits->maxHeaderCount,
                          .maxBodyBytes = shared.limits->maxBodyBytes}),
      m_connectionId(nextConnectionId()) {
    m_socket.setParent(this); // Qt parent ownership: the socket lives as long as the connection
    m_socket.setReadBufferSize(readChunk);
    for (QTimer* timer : {&m_headerTimer, &m_bodyTimer, &m_idleTimer, &m_jsonTimer}) {
        timer->setSingleShot(true);
    }
    m_headerTimer.setInterval(shared.limits->headerTimeout);
    m_bodyTimer.setInterval(shared.limits->bodyTimeout);
    m_idleTimer.setInterval(shared.limits->idleTimeout);
    m_closingTimer.setSingleShot(true);
    m_closingTimer.setInterval(shared.limits->closingTimeout);
    m_jsonTimer.setInterval(shared.limits->jsonResponseWindow);
    m_heartbeatTimer.setInterval(shared.limits->sseHeartbeat);

    connect(&m_headerTimer, &QTimer::timeout, this, [this] {
        qCInfo(hio::log::lcMcp) << "MCP HTTP: request headers not received in time";
        refuse(408, "Request headers not received in time");
    });
    connect(&m_bodyTimer, &QTimer::timeout, this, [this] {
        qCInfo(hio::log::lcMcp) << "MCP HTTP: request body not received in time";
        refuse(408, "Request body not received in time");
    });
    connect(&m_idleTimer, &QTimer::timeout, this,
            [this] { closeConnection(CloseReason::PeerClosed); });
    connect(&m_closingTimer, &QTimer::timeout, this, [this] {
        qCInfo(hio::log::lcMcp) << "MCP HTTP: refused connection did not close in time; aborted";
        closeConnection(CloseReason::PeerClosed);
    });
    connect(&m_jsonTimer, &QTimer::timeout, this, [this] {
        if (auto channel = m_active.lock(); channel && channel->canStream()) {
            beginStream(m_serial);
        }
    });
    connect(&m_heartbeatTimer, &QTimer::timeout, this, [this] {
        if (m_phase == Phase::Streaming) {
            writeChunk(": heartbeat\n\n");
        }
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, &HttpConnection::onReadyRead);
    connect(&m_socket, &QTcpSocket::disconnected, this,
            [this] { closeConnection(CloseReason::PeerClosed); });
    connect(&m_socket, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) { closeConnection(CloseReason::PeerClosed); });
    m_headerTimer.start();
    if (m_socket.bytesAvailable() > 0) {
        QTimer::singleShot(0, this, &HttpConnection::onReadyRead);
    }
}

HttpConnection::~HttpConnection() {
    if (m_phase != Phase::Closed) {
        closeConnection(CloseReason::Shutdown);
    }
}

void HttpConnection::shutdown() { closeConnection(CloseReason::Shutdown); }

bool HttpConnection::isCurrent(std::uint64_t serial) const noexcept {
    return serial == m_serial && (m_phase == Phase::Handling || m_phase == Phase::Streaming);
}

void HttpConnection::onReadyRead() {
    while (m_phase == Phase::Reading) {
        if (m_inbox.isEmpty()) {
            m_inbox = m_socket.read(readChunk);
            if (m_inbox.isEmpty()) {
                return;
            }
            if (!m_parser.started()) {
                m_idleTimer.stop();
                if (!m_headerTimer.isActive()) {
                    m_headerTimer.start();
                }
            }
        }
        const std::size_t consumed = m_parser.feed(m_inbox);
        m_inbox.remove(0, static_cast<qsizetype>(consumed));
        switch (m_parser.state()) {
        case HttpParser::State::Error:
            refuseParse();
            return;
        case HttpParser::State::HeadersComplete:
            m_headerTimer.stop();
            if (!checkRequest()) {
                return;
            }
            m_parser.acceptBody();
            if (m_parser.state() == HttpParser::State::Error) {
                refuseParse();
                return;
            }
            if (m_parser.state() == HttpParser::State::Body) {
                m_bodyTimer.start();
            } else if (m_parser.state() == HttpParser::State::Complete) {
                dispatch(); // no body
            }
            break;
        case HttpParser::State::Complete:
            m_bodyTimer.stop();
            dispatch();
            break;
        case HttpParser::State::Headers:
        case HttpParser::State::Body:
            break;
        }
    }
}

// Steps 1-3 of the check order (who may talk to the server); checkShape() does the rest.
bool HttpConnection::checkRequest() {
    const HttpRequest& request = m_parser.request();
    const QByteArray port = QByteArray::number(m_shared->port);

    // 1. Host (DNS rebinding, ADR-0102)
    const QByteArray host = request.header("host").value_or(QByteArray()).toLower();
    if (request.headerCount("host") != 1 ||
        (host != "127.0.0.1:" + port && host != "localhost:" + port)) {
        refuse(421, "Invalid Host header");
        return false;
    }
    // 2. Origin (absent = non-browser client)
    if (const auto origin = request.header("origin")) {
        const QByteArray value = origin->toLower();
        if (request.headerCount("origin") != 1 ||
            (value != "http://127.0.0.1:" + port && value != "http://localhost:" + port)) {
            refuse(403, "Origin not allowed");
            return false;
        }
    }
    // Sec-Fetch-Site (review R1): a browser request from another site carries it even when it
    // sends no Origin (no-cors GET). Refused before the token step, so a web page cannot fill
    // the auth-failure window and lock real clients out. "same-origin" and "none" pass.
    if (const auto site = request.header("sec-fetch-site")) {
        const QByteArray value = site->trimmed().toLower();
        if (request.headerCount("sec-fetch-site") != 1 || value == "cross-site" ||
            value == "same-site") {
            refuse(403, "Cross-site request not allowed");
            return false;
        }
    }
    // 3. Lockout, then the bearer token (compared in constant time, never logged)
    if (m_shared->auth.locked()) {
        refuse(429, "Too many failed authentications",
               "Retry-After: " +
                   QByteArray::number((m_shared->limits->authLockout.count() + 999) / 1000) +
                   "\r\n");
        return false;
    }
    const QByteArray authorization = request.header("authorization").value_or(QByteArray());
    const qsizetype space = authorization.indexOf(' ');
    const bool bearer =
        space > 0 && authorization.left(space).toLower() == "bearer" &&
        request.headerCount("authorization") == 1 &&
        constantTimeEquals(authorization.mid(space + 1).trimmed().toStdString(), m_shared->token);
    if (!bearer) {
        // Only a request that tried to authenticate counts towards the lockout (review R1): a
        // request without Authorization is refused but cannot lock other clients out.
        if (request.headerCount("authorization") > 0) {
            m_shared->auth.recordFailure();
        }
        refuse(401, "Missing or invalid bearer token", "WWW-Authenticate: Bearer\r\n");
        return false;
    }
    return checkShape();
}

bool HttpConnection::checkShape() {
    const HttpRequest& request = m_parser.request();
    // 4. Path and method
    if (request.path() != "/mcp") {
        refuse(404, "Not found");
        return false;
    }
    const QByteArray& method = request.method;
    if (method != "POST" && method != "GET" && method != "DELETE") {
        refuse(405, "Method not allowed", "Allow: POST, GET, DELETE\r\n");
        return false;
    }
    // 5. Size limits
    if (request.hasTransferEncoding || (method == "POST" && !request.contentLength)) {
        refuse(411, "Content-Length required (chunked requests are not supported)");
        return false;
    }
    if (request.contentLength.value_or(0) > m_shared->limits->maxBodyBytes) {
        refuse(413, "Request body too large");
        return false;
    }
    // 6. Content-Type and Accept
    const QByteArrayList accept = mediaTypes(request.header("accept").value_or(QByteArray()));
    if (method == "POST") {
        const QByteArrayList contentType =
            mediaTypes(request.header("content-type").value_or(QByteArray()));
        if (contentType.size() != 1 || contentType.front() != "application/json") {
            refuse(415, "Content-Type must be application/json");
            return false;
        }
        // "*/*" is accepted like application/json (lenient for simple clients; accepted in the
        // review of plan 0013). Without text/event-stream the answer is always JSON.
        if (!accept.contains("application/json") && !accept.contains("text/event-stream") &&
            !accept.contains("*/*")) {
            refuse(406, "Accept must include application/json or text/event-stream");
            return false;
        }
    } else if (method == "GET" && !accept.contains("text/event-stream")) {
        refuse(406, "Accept must include text/event-stream");
        return false;
    }
    return true;
}

void HttpConnection::dispatch() {
    const HttpRequest& request = m_parser.request();
    const TransportInfo transport{.kind = TransportKind::Http,
                                  .connectionId = m_connectionId,
                                  .protocolVersionHeader =
                                      headerString(request, protocol::header::protocolVersion),
                                  .methodHeader = headerString(request, protocol::header::method),
                                  .nameHeader = headerString(request, protocol::header::name),
                                  .sessionId = headerString(request, protocol::header::sessionId)};
    ++m_serial;
    m_phase = Phase::Handling;
    m_idleTimer.stop();

    const QByteArrayList accept = mediaTypes(request.header("accept").value_or(QByteArray()));
    const bool canStream = accept.contains("text/event-stream");
    if (request.method == "POST") {
        auto channel = std::make_shared<HttpReplyChannel>(*this, m_serial, canStream);
        m_active = channel;
        m_jsonTimer.start();
        const QByteArray body = request.body;
        m_shared->server->handle(body, channel, transport);
        return;
    }
    if (request.method == "GET") {
        auto channel = std::make_shared<HttpReplyChannel>(*this, m_serial, true);
        m_active = channel;
        const ReplyStatus status = m_shared->server->openSessionStream(transport, channel);
        if (status != ReplyStatus::Ok) {
            m_active.reset();
            channel->complete(QJsonObject(), ReplyMeta{.status = status, .sessionId = {}});
        }
        return;
    }
    // DELETE
    const ReplyStatus status = m_shared->server->endSession(transport);
    sendSimple(httpStatus(status));
    afterResponse();
}

void HttpConnection::beginStream(std::uint64_t serial) {
    if (serial != m_serial || m_phase != Phase::Handling) {
        return;
    }
    m_jsonTimer.stop();
    m_phase = Phase::Streaming;
    writeRaw(statusLine(200) + "Content-Type: text/event-stream\r\n"
                               "Cache-Control: no-cache\r\n"
                               "X-Accel-Buffering: no\r\n"
                               "Transfer-Encoding: chunked\r\n\r\n");
    m_heartbeatTimer.start();
}

void HttpConnection::sendEvent(std::uint64_t serial, const QByteArray& json) {
    if (serial != m_serial || m_phase != Phase::Streaming) {
        return;
    }
    writeChunk("event: message\ndata: " + json + "\n\n");
}

void HttpConnection::finishResponse(std::uint64_t serial, const QJsonObject& response,
                                    const ReplyMeta& meta) {
    if (serial != m_serial || (m_phase != Phase::Handling && m_phase != Phase::Streaming)) {
        return;
    }
    m_jsonTimer.stop();
    m_active.reset();
    if (m_phase == Phase::Streaming) {
        if (!response.isEmpty()) {
            writeChunk("event: message\ndata: " + serialize(response) + "\n\n");
        }
        m_heartbeatTimer.stop();
        if (m_phase == Phase::Streaming) {
            writeRaw("0\r\n\r\n");
        }
    } else {
        const int status = httpStatus(meta.status);
        QByteArray headers;
        if (meta.sessionId) {
            headers += "Mcp-Session-Id: " + QByteArray::fromStdString(*meta.sessionId) + "\r\n";
        }
        if (meta.status == ReplyStatus::Accepted || response.isEmpty()) {
            writeRaw(statusLine(status) + headers + "Content-Length: 0\r\n\r\n");
        } else {
            const QByteArray body = serialize(response);
            writeRaw(statusLine(status) + headers +
                     "Content-Type: application/json\r\nContent-Length: " +
                     QByteArray::number(body.size()) + "\r\n\r\n" + body);
        }
    }
    afterResponse();
}

void HttpConnection::afterResponse() {
    if (m_phase == Phase::Closed || m_phase == Phase::Closing) {
        return;
    }
    m_phase = Phase::Reading;
    m_parser.reset();
    m_idleTimer.start();
    if (!m_inbox.isEmpty() || m_socket.bytesAvailable() > 0) {
        QTimer::singleShot(0, this, &HttpConnection::onReadyRead);
    }
}

void HttpConnection::sendSimple(int status, const QByteArray& extraHeaders) {
    writeRaw(statusLine(status) + extraHeaders + "Content-Length: 0\r\n\r\n");
}

void HttpConnection::refuse(int status, const QByteArray& reason, const QByteArray& extraHeaders) {
    if (m_phase == Phase::Closed || m_phase == Phase::Closing) {
        return;
    }
    qCInfo(hio::log::lcMcp).noquote() << "MCP HTTP: refused a request with" << status << "("
                                      << QString::fromLatin1(reason) << ")";
    const QByteArray body = serialize(
        errorResponse(QJsonValue::Undefined, RpcError{.code = protocol::errc::invalidRequest,
                                                      .message = QString::fromLatin1(reason)}));
    m_headerTimer.stop();
    m_bodyTimer.stop();
    m_idleTimer.stop();
    writeRaw(statusLine(status) + extraHeaders +
             "Content-Type: application/json\r\nConnection: close\r\nContent-Length: " +
             QByteArray::number(body.size()) + "\r\n\r\n" + body);
    if (m_phase == Phase::Closed) {
        return;
    }
    m_phase = Phase::Closing;
    // Flushes, then `disconnected` closes the connection. A client that does not read never lets
    // the flush finish: the closing timer aborts it (review R4).
    m_closingTimer.start();
    m_socket.disconnectFromHost();
}

void HttpConnection::refuseParse() {
    switch (m_parser.error()) {
    case HttpParseError::HeaderTooLarge:
        refuse(431, "Request headers too large");
        return;
    case HttpParseError::VersionNotSupported:
        refuse(505, "Only HTTP/1.1 is supported");
        return;
    case HttpParseError::BodyTooLarge:
        refuse(413, "Request body too large");
        return;
    case HttpParseError::BadRequest:
    case HttpParseError::None:
        refuse(400, "Malformed HTTP request");
        return;
    }
}

void HttpConnection::writeRaw(const QByteArray& bytes) {
    if (m_phase == Phase::Closed) {
        return;
    }
    if (std::cmp_greater(m_socket.bytesToWrite(), m_shared->limits->maxSendQueueBytes)) {
        qCWarning(hio::log::lcMcp) << "MCP HTTP: the client does not read its responses; "
                                      "connection dropped";
        closeConnection(CloseReason::SlowClient);
        return;
    }
    m_socket.write(bytes);
}

void HttpConnection::writeChunk(const QByteArray& data) {
    writeRaw(QByteArray::number(data.size(), 16) + "\r\n" + data + "\r\n");
}

void HttpConnection::closeConnection(CloseReason reason) {
    if (m_phase == Phase::Closed) {
        return;
    }
    m_phase = Phase::Closed;
    for (QTimer* timer : {&m_headerTimer, &m_bodyTimer, &m_idleTimer, &m_jsonTimer,
                          &m_heartbeatTimer, &m_closingTimer}) {
        timer->stop();
    }
    if (auto channel = m_active.lock()) {
        m_active.reset();
        channel->peerClosed(reason);
    }
    m_socket.disconnect(this);
    m_socket.abort();
    emit closed();
}

} // namespace hio::mcp::detail
