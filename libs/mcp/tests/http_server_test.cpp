// HttpServer end to end with a raw QTcpSocket client (plan/features/0013 "Tests: HTTP"): every
// check-order branch with its status code, Origin and Host variants, token and lockout, header and
// body limits, chunked requests, timeouts (shrunk), keep-alive, JSON vs SSE, heartbeat, stream
// close cancels, slow clients, the connection limit and port-in-use; Sec-Fetch-Site and the
// lockout counting (review R1), the closing timeout of refused connections (R4).

#include "fake_tools.h"
#include "hio/core/error.h"
#include "hio/mcp/http_server.h"
#include "hio/mcp/json_rpc.h"
#include "hio/mcp/limits.h"
#include "hio/mcp/server.h"
#include "hio/mcp/token.h"
#include "mcp_test_support.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QByteArrayView>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QList>
#include <QObject>
#include <QTcpSocket>
#include <QtCore/qhashfunctions.h>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace hio::mcp {
namespace {

using namespace std::chrono_literals;
using test::runFor;
using test::runUntil;

struct Response {
    int status = 0;
    std::map<QByteArray, QByteArray> headers; // lower-case names
    QByteArray body;                          // de-chunked
    bool complete = false;                    // the whole body was received
    bool received = false; // the head was received (partial()) / the response is complete

    [[nodiscard]] QByteArray header(const char* name) const {
        auto it = headers.find(QByteArray(name));
        return it == headers.end() ? QByteArray() : it->second;
    }
    [[nodiscard]] QJsonObject json() const { return QJsonDocument::fromJson(body).object(); }
    // The "data:" payloads of SSE events.
    [[nodiscard]] std::vector<QJsonObject> events() const {
        std::vector<QJsonObject> result;
        for (const QByteArray& line : body.split('\n')) {
            if (line.startsWith("data: ")) {
                result.push_back(QJsonDocument::fromJson(line.mid(6)).object());
            }
        }
        return result;
    }
};

// Parses one response at the start of `data` (as far as received); `received` is false before the
// head ends.
Response parseResponse(const QByteArray& data, qsizetype* used = nullptr) {
    Response response;
    const qsizetype headEnd = data.indexOf("\r\n\r\n");
    if (headEnd < 0) {
        return response;
    }
    response.received = true;
    const QList<QByteArray> lines = data.left(headEnd).split('\n');
    response.status = lines.front().split(' ').value(1).toInt();
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const qsizetype colon = line.indexOf(':');
        response.headers[line.left(colon).toLower()] = line.mid(colon + 1).trimmed();
    }
    qsizetype pos = headEnd + 4;
    if (response.header("transfer-encoding") == "chunked") {
        while (true) {
            const qsizetype lineEnd = data.indexOf("\r\n", pos);
            if (lineEnd < 0) {
                break;
            }
            bool ok = false;
            const qsizetype size = data.mid(pos, lineEnd - pos).toLongLong(&ok, 16);
            if (!ok || data.size() < lineEnd + 2 + size + 2) {
                break;
            }
            pos = lineEnd + 2 + size + 2;
            if (size == 0) {
                response.complete = true;
                break;
            }
            response.body += data.mid(lineEnd + 2, size);
        }
    } else {
        const qsizetype length = response.header("content-length").toLongLong();
        response.body = data.mid(pos, length);
        response.complete = response.body.size() == length;
        pos += response.body.size();
    }
    if (used != nullptr) {
        *used = pos;
    }
    return response;
}

class Client {
public:
    explicit Client(quint16 port) {
        m_socket.connectToHost(QHostAddress(QHostAddress::LocalHost), port);
        QObject::connect(&m_socket, &QTcpSocket::readyRead, [this] {
            if (m_reading) {
                m_received += m_socket.readAll();
            }
        });
        // Connected, or already closed again (a refused 17th connection).
        runUntil([this] {
            return m_socket.state() != QAbstractSocket::HostLookupState &&
                   m_socket.state() != QAbstractSocket::ConnectingState;
        });
    }

    void send(const QByteArray& bytes) { m_socket.write(bytes); }

    // Waits for the next complete response and removes it from the buffer.
    // `received` is false when none arrived in time.
    Response response(std::chrono::milliseconds timeout = 5s) {
        Response result;
        runUntil(
            [&] {
                qsizetype used = 0;
                result = parseResponse(m_received, &used);
                if (result.received && result.complete) {
                    m_received.remove(0, used);
                    return true;
                }
                return !connected() && !result.received;
            },
            timeout);
        result.received = result.received && result.complete;
        return result;
    }

    // The response received so far (streams).
    [[nodiscard]] Response partial() const { return parseResponse(m_received); }

    [[nodiscard]] bool connected() const {
        return m_socket.state() == QAbstractSocket::ConnectedState;
    }
    bool waitClosed(std::chrono::milliseconds timeout = 5s) {
        return runUntil([this] { return !connected(); }, timeout);
    }
    QTcpSocket& socket() { return m_socket; }
    void close() { m_socket.abort(); }
    // Stops reading: the kernel buffers fill up and the server's send queue grows.
    void stopReading() {
        m_reading = false;
        m_socket.setReadBufferSize(1);
    }

private:
    bool m_reading = true;
    QTcpSocket m_socket;
    QByteArray m_received;
};

QJsonObject modernMeta(const QJsonObject& extra = {}) {
    QJsonObject meta{
        {QStringLiteral("io.modelcontextprotocol/protocolVersion"), QStringLiteral("2026-07-28")},
        {QStringLiteral("io.modelcontextprotocol/clientCapabilities"), QJsonObject()},
        {QStringLiteral("io.modelcontextprotocol/clientInfo"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("http-test")},
                     {QStringLiteral("version"), QStringLiteral("1")}}}};
    for (auto it = extra.begin(); it != extra.end(); ++it) {
        meta.insert(it.key(), it.value());
    }
    return meta;
}

QByteArray toolCallBody(const char* tool, const QJsonObject& args = {},
                        const QJsonObject& metaExtra = {}) {
    return serialize(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("tools/call")},
        {QStringLiteral("params"), QJsonObject{{QStringLiteral("name"), QLatin1StringView(tool)},
                                               {QStringLiteral("arguments"), args},
                                               {QStringLiteral("_meta"), modernMeta(metaExtra)}}}});
}

class HttpServerTest : public ::testing::Test {
public:
    void SetUp() override { start(Limits{}); }

    void start(const Limits& limits) {
        http.reset();
        server.reset();
        ServerOptions options;
        options.limits = limits;
        options.version = QStringLiteral("test");
        server = std::make_unique<McpServer>(test::makeFakeRegistry(), consent, activity,
                                             std::move(options));
        http = std::make_unique<HttpServer>(*server, HttpServerOptions{.port = 0, .token = token});
        auto listening = http->listen();
        ASSERT_TRUE(listening.has_value()) << listening.error().detail;
        port = http->port();
        ASSERT_NE(port, 0);
    }

    // A full request; `overrides` replace or (empty value) remove default headers.
    [[nodiscard]] QByteArray request(const QByteArray& method, const QByteArray& body,
                                     const std::map<QByteArray, QByteArray>& overrides = {},
                                     const QByteArray& target = "/mcp") const {
        std::map<QByteArray, QByteArray> headers{
            {"Host", "127.0.0.1:" + QByteArray::number(port)},
            {"Authorization", "Bearer " + QByteArray::fromStdString(token)},
            {"Content-Type", "application/json"},
            {"Accept", "application/json, text/event-stream"},
            {"Content-Length", QByteArray::number(body.size())},
            {"MCP-Protocol-Version", "2026-07-28"},
            {"Mcp-Method", "tools/call"},
            {"Mcp-Name", "echo"},
        };
        for (const auto& [name, value] : overrides) {
            if (value.isNull()) {
                headers.erase(name);
            } else {
                headers[name] = value;
            }
        }
        QByteArray text = method + ' ' + target + " HTTP/1.1\r\n";
        for (const auto& [name, value] : headers) {
            text += name + ": " + value + "\r\n";
        }
        return text + "\r\n" + body;
    }

    [[nodiscard]] QByteArray echo() const {
        return request("POST",
                       toolCallBody("echo", {{QStringLiteral("text"), QStringLiteral("x")}}));
    }

    [[nodiscard]] int statusOf(const QByteArray& raw) const {
        Client client(port);
        client.send(raw);
        auto response = client.response();
        return response.received ? response.status : -1;
    }

    test::FakeConsentBroker consent;
    test::RecordingActivitySink activity;
    std::string token = TokenStore::generateToken();
    std::unique_ptr<McpServer> server;
    std::unique_ptr<HttpServer> http;
    quint16 port = 0;
};

TEST_F(HttpServerTest, JsonResponseAndKeepAlive) {
    Client client(port);
    for (int i = 0; i < 3; ++i) {
        client.send(echo());
        auto response = client.response();
        ASSERT_TRUE(response.received);
        EXPECT_EQ(response.status, 200);
        EXPECT_EQ(response.header("content-type"), "application/json");
        EXPECT_EQ(response.json()
                      .value(QStringLiteral("result"))
                      .toObject()
                      .value(QStringLiteral("structuredContent"))
                      .toObject()
                      .value(QStringLiteral("text"))
                      .toString(),
                  QStringLiteral("x"));
    }
    // Two pipelined requests are answered in order.
    client.send(echo() + echo());
    EXPECT_TRUE(client.response().received);
    EXPECT_TRUE(client.response().received);
    EXPECT_TRUE(client.connected());
    EXPECT_EQ(http->connectionCount(), 1U);
}

TEST_F(HttpServerTest, HostAndOriginChecks) {
    const QByteArray p = QByteArray::number(port);
    EXPECT_EQ(statusOf(request("POST", "{}", {{"Host", {}}})), 421);
    EXPECT_EQ(statusOf(request("POST", "{}", {{"Host", "example.com:" + p}})), 421);
    EXPECT_EQ(statusOf(request("POST", "{}", {{"Host", "127.0.0.1"}})), 421);
    EXPECT_EQ(statusOf(request("POST", "{}", {{"Host", "127.0.0.1:1"}})), 421);
    EXPECT_EQ(statusOf(request("POST", "{}", {{"Host", "[::1]:" + p}})), 421);
    const QByteArray body = toolCallBody("echo", {{QStringLiteral("text"), QStringLiteral("x")}});
    EXPECT_EQ(statusOf(request("POST", body, {{"Host", "localhost:" + p}})), 200);
    EXPECT_EQ(statusOf(request("POST", body, {{"Host", "LocalHost:" + p}})), 200);

    EXPECT_EQ(statusOf(request("POST", body, {{"Origin", "null"}})), 403);
    EXPECT_EQ(statusOf(request("POST", body, {{"Origin", "http://127.0.0.1:1"}})), 403);
    EXPECT_EQ(statusOf(request("POST", body, {{"Origin", "https://127.0.0.1:" + p}})), 403);
    EXPECT_EQ(statusOf(request("POST", body, {{"Origin", "http://[::1]:" + p}})), 403);
    EXPECT_EQ(statusOf(request("POST", body, {{"Origin", "http://evil.example"}})), 403);
    EXPECT_EQ(statusOf(request("POST", body, {{"Origin", "http://localhost:" + p}})), 200);
    EXPECT_EQ(statusOf(request("POST", body, {{"Origin", "http://127.0.0.1:" + p}})), 200);
}

TEST_F(HttpServerTest, CheckOrder) {
    const QByteArray p = QByteArray::number(port);
    // Host before Origin before token before path before size before content type before accept.
    EXPECT_EQ(statusOf(request("POST", "{}",
                               {{"Host", "x:1"}, {"Origin", "null"}, {"Authorization", {}}})),
              421);
    EXPECT_EQ(statusOf(request("POST", "{}", {{"Origin", "null"}, {"Authorization", {}}})), 403);
    EXPECT_EQ(statusOf(request("POST", "{}", {{"Authorization", {}}}, "/other")), 401);
    EXPECT_EQ(statusOf(request("POST", "{}", {{"Content-Type", "text/plain"}}, "/other")), 404);
    EXPECT_EQ(statusOf(request("PUT", "{}", {{"Content-Type", "text/plain"}})), 405);
    EXPECT_EQ(statusOf(request("POST", "{}",
                               {{"Content-Length", {}},
                                {"Transfer-Encoding", "chunked"},
                                {"Content-Type", "text/plain"}})),
              411);
    EXPECT_EQ(statusOf(request("POST", "", {{"Content-Length", {}}})), 411);
    EXPECT_EQ(
        statusOf(request("POST", "{}", {{"Content-Type", "text/plain"}, {"Accept", "text/html"}})),
        415);
    EXPECT_EQ(statusOf(request("POST", "{}", {{"Accept", "text/html"}})), 406);
    EXPECT_EQ(statusOf(request("POST", "{}", {{"Accept", {}}})), 406);
    EXPECT_EQ(statusOf(request("OPTIONS", "")), 405);
    EXPECT_EQ(statusOf(request("POST", "{not json")), 400);

    Client client(port);
    client.send(request("POST",
                        toolCallBody("echo", {{QStringLiteral("text"), QStringLiteral("x")}}),
                        {{"Mcp-Name", "address"}}));
    auto mismatch = client.response();
    ASSERT_TRUE(mismatch.received);
    EXPECT_EQ(mismatch.status, 400);
    EXPECT_EQ(mismatch.json()
                  .value(QStringLiteral("error"))
                  .toObject()
                  .value(QStringLiteral("code"))
                  .toInt(),
              -32020);
    EXPECT_TRUE(client.connected()); // protocol errors keep the connection

    Client refused(port);
    refused.send(request("POST", "{}", {{"Authorization", "Basic abc"}}));
    auto unauthorized = refused.response();
    ASSERT_TRUE(unauthorized.received);
    EXPECT_EQ(unauthorized.status, 401);
    EXPECT_EQ(unauthorized.header("www-authenticate"), "Bearer");
    EXPECT_EQ(unauthorized.header("connection"), "close");
    EXPECT_TRUE(refused.waitClosed());
}

TEST_F(HttpServerTest, CrossSiteBrowserRequestsAreRefusedBeforeTheToken) {
    Limits limits;
    limits.authFailureLimit = 2;
    start(limits);
    const QByteArray body = toolCallBody("echo", {{QStringLiteral("text"), QStringLiteral("x")}});
    // A no-cors GET of a web page: no Origin, Sec-Fetch-Site cross-site, no token.
    for (const QByteArray& site :
         {QByteArray("cross-site"), QByteArray("same-site"), QByteArray("Cross-Site")}) {
        for (int i = 0; i < 3; ++i) {
            EXPECT_EQ(statusOf(request("GET", "",
                                       {{"Sec-Fetch-Site", site},
                                        {"Authorization", "Bearer wrong"},
                                        {"Content-Length", {}},
                                        {"Content-Type", {}},
                                        {"Accept", "text/event-stream"}})),
                      403);
        }
        EXPECT_EQ(statusOf(request("POST", body, {{"Sec-Fetch-Site", site}})), 403);
    }
    // No lockout: the refused requests never reached the token step.
    EXPECT_EQ(statusOf(echo()), 200);
    // Same-origin and user-initiated requests pass on to the token check.
    EXPECT_EQ(statusOf(request("POST", body, {{"Sec-Fetch-Site", "same-origin"}})), 200);
    EXPECT_EQ(statusOf(request("POST", body, {{"Sec-Fetch-Site", "none"}})), 200);
    // Order: Host before Sec-Fetch-Site before the token.
    EXPECT_EQ(statusOf(request("POST", body, {{"Sec-Fetch-Site", "cross-site"}, {"Host", "x:1"}})),
              421);
    EXPECT_EQ(
        statusOf(request("POST", body, {{"Sec-Fetch-Site", "cross-site"}, {"Authorization", {}}})),
        403);
}

TEST_F(HttpServerTest, RefusedConnectionThatDoesNotReadIsAborted) {
    Limits limits;
    limits.closingTimeout = 200ms;
    limits.maxSendQueueBytes = std::size_t{256} * 1024 * 1024; // not the slow-client path
    limits.maxBodyBytes = std::size_t{32} * 1024 * 1024;
    start(limits);
    Client client(port);
    client.socket().setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4096);
    client.stopReading();
    // A protocol error that echoes a 24 MB id (fills every buffer), then a refused request
    // (pipelined): the refusal cannot be flushed while the client does not read.
    const QByteArray big = serialize(
        QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                    {QStringLiteral("id"), QString(qsizetype{24} * 1024 * 1024, QLatin1Char('i'))},
                    {QStringLiteral("method"), QStringLiteral("tools/list")},
                    {QStringLiteral("params"), QJsonObject()}});
    client.send(
        request("POST", big, {{"MCP-Protocol-Version", {}}, {"Mcp-Method", {}}, {"Mcp-Name", {}}}) +
        request("POST", "{}", {{"Host", "x:1"}}));
    ASSERT_TRUE(runUntil([&] { return http->connectionCount() == 1; }));
    // Aborted after the closing timeout although the client never reads (the client does not
    // notice while its buffers are full, so only the server side is checked).
    EXPECT_TRUE(runUntil([&] { return http->connectionCount() == 0; }, 10s));
}

TEST_F(HttpServerTest, MalformedVersionAndSizeLimits) {
    EXPECT_EQ(statusOf("GET /mcp HTTP/1.0\r\nHost: x\r\n\r\n"), 505);
    EXPECT_EQ(statusOf("GARBAGE\r\n\r\n"), 400);

    // Headers: exactly 16 KiB pass the parser, one byte more is 431.
    const QByteArray base = request("POST", "", {{"Content-Length", "0"}, {"X-Pad", "P"}});
    const qsizetype padTo16k =
        (qsizetype{16} * 1024) - base.size() + 1; // X-Pad value length for 16 KiB
    const QByteArray fits =
        request("POST", "", {{"Content-Length", "0"}, {"X-Pad", QByteArray(padTo16k, 'p')}});
    ASSERT_EQ(fits.size(), qsizetype{16} * 1024);
    EXPECT_EQ(statusOf(fits), 400); // passes every check; the empty body is a JSON parse error
    EXPECT_EQ(statusOf(request(
                  "POST", "", {{"Content-Length", "0"}, {"X-Pad", QByteArray(padTo16k + 1, 'p')}})),
              431);

    // Body: 4 MiB + 1 is refused before it is sent.
    EXPECT_EQ(statusOf(request("POST", "",
                               {{"Content-Length", QByteArray::number((4 * 1024 * 1024) + 1)}})),
              413);
    // Exactly 4 MiB is accepted.
    QByteArray big = toolCallBody("echo", {{QStringLiteral("text"), QStringLiteral("x")}});
    const qsizetype padding = (qsizetype{4} * 1024 * 1024) - big.size() - 9;
    big.insert(1, R"("pad":")" + QByteArray(padding, 'x') + R"(",)");
    ASSERT_EQ(big.size(), qsizetype{4} * 1024 * 1024);
    EXPECT_EQ(statusOf(request("POST", big)), 200);
}

TEST_F(HttpServerTest, LockoutAfterRepeatedTokenFailures) {
    Limits limits;
    limits.authLockout = 300ms;
    start(limits);
    for (int i = 0; i < 9; ++i) {
        EXPECT_EQ(statusOf(request("POST", "{}", {{"Authorization", "Bearer wrong"}})), 401);
    }
    EXPECT_EQ(statusOf(echo()), 200); // 9 failures: still open
    // Requests without Authorization are refused but never count (review R1).
    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(statusOf(request("POST", "{}", {{"Authorization", {}}})), 401);
    }
    EXPECT_EQ(statusOf(echo()), 200);
    EXPECT_EQ(statusOf(request("POST", "{}", {{"Authorization", "Basic abc"}})), 401);
    Client client(port);
    client.send(echo());
    auto locked = client.response();
    ASSERT_TRUE(locked.received);
    EXPECT_EQ(locked.status, 429);
    EXPECT_FALSE(locked.header("retry-after").isEmpty());
    runFor(350ms);
    EXPECT_EQ(statusOf(echo()), 200);
}

TEST_F(HttpServerTest, SlowlorisAndIdleTimeouts) {
    Limits limits;
    limits.headerTimeout = 150ms;
    limits.bodyTimeout = 150ms;
    limits.idleTimeout = 150ms;
    start(limits);
    Client slowHeaders(port);
    slowHeaders.send("POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:");
    auto timeout = slowHeaders.response(2s);
    ASSERT_TRUE(timeout.received);
    EXPECT_EQ(timeout.status, 408);
    EXPECT_TRUE(slowHeaders.waitClosed());

    Client slowBody(port);
    const QByteArray full = echo();
    slowBody.send(full.left(full.size() - 5));
    auto bodyTimeout = slowBody.response(2s);
    ASSERT_TRUE(bodyTimeout.received);
    EXPECT_EQ(bodyTimeout.status, 408);

    Client idle(port);
    idle.send(echo());
    EXPECT_TRUE(idle.response().received);
    EXPECT_TRUE(idle.waitClosed(2s));
}

TEST_F(HttpServerTest, NotificationIsAccepted) {
    const QByteArray body = serialize(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), QStringLiteral("notifications/cancelled")},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("requestId"), 1}, {QStringLiteral("_meta"), modernMeta()}}}});
    Client client(port);
    client.send(
        request("POST", body, {{"Mcp-Method", "notifications/cancelled"}, {"Mcp-Name", {}}}));
    auto response = client.response();
    ASSERT_TRUE(response.received);
    EXPECT_EQ(response.status, 202);
    EXPECT_TRUE(response.body.isEmpty());
}

TEST_F(HttpServerTest, SseForProgressAndSlowCalls) {
    Limits limits;
    limits.jsonResponseWindow = 50ms;
    limits.sseHeartbeat = 40ms;
    limits.progressInterval = 0ms;
    start(limits);
    {
        Client client(port);
        client.send(request(
            "POST",
            toolCallBody("slow", {{QStringLiteral("step_ms"), 20}, {QStringLiteral("steps"), 3}},
                         {{QStringLiteral("progressToken"), 7}}),
            {{"Mcp-Name", "slow"}}));
        auto response = client.response();
        ASSERT_TRUE(response.received);
        EXPECT_EQ(response.status, 200);
        EXPECT_EQ(response.header("content-type"), "text/event-stream");
        EXPECT_EQ(response.header("x-accel-buffering"), "no");
        const auto events = response.events();
        ASSERT_EQ(events.size(), 4U);
        EXPECT_EQ(events[0].value(QStringLiteral("method")).toString(),
                  QStringLiteral("notifications/progress"));
        EXPECT_TRUE(events[3].contains(QStringLiteral("result")));
        // The connection is reused after the stream.
        client.send(echo());
        EXPECT_EQ(client.response().status, 200);
    }
    {
        // No progress token, longer than the JSON window: SSE with heartbeats.
        Client client(port);
        client.send(request("POST", toolCallBody("slow", {{QStringLiteral("step_ms"), 200}}),
                            {{"Mcp-Name", "slow"}}));
        auto response = client.response();
        ASSERT_TRUE(response.received);
        EXPECT_EQ(response.header("content-type"), "text/event-stream");
        EXPECT_TRUE(response.body.contains(": heartbeat\n\n"));
        EXPECT_EQ(response.events().size(), 1U);
    }
    {
        // A client that accepts JSON only waits for the JSON response.
        Client client(port);
        client.send(request("POST", toolCallBody("slow", {{QStringLiteral("step_ms"), 120}}),
                            {{"Mcp-Name", "slow"}, {"Accept", "application/json"}}));
        auto response = client.response();
        ASSERT_TRUE(response.received);
        EXPECT_EQ(response.header("content-type"), "application/json");
    }
}

TEST_F(HttpServerTest, ClosingTheStreamCancelsTheCall) {
    Limits limits;
    limits.jsonResponseWindow = 20ms;
    start(limits);
    Client client(port);
    client.send(request("POST", toolCallBody("slow", {{QStringLiteral("step_ms"), 5000}}),
                        {{"Mcp-Name", "slow"}}));
    ASSERT_TRUE(runUntil([&] { return client.partial().received; }));
    EXPECT_EQ(server->activeCalls(), 1U);
    client.close();
    ASSERT_TRUE(runUntil([&] { return server->activeCalls() == 0; }));
    ASSERT_FALSE(activity.entries.empty());
    EXPECT_EQ(activity.entries.back().result, "cancelled");
}

TEST_F(HttpServerTest, SlowClientIsDropped) {
    Limits limits;
    limits.maxSendQueueBytes = std::size_t{64} * 1024;
    start(limits);
    Client client(port);
    client.stopReading();
    const QByteArray body = serialize(
        QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                    {QStringLiteral("id"), 1},
                    {QStringLiteral("method"), QStringLiteral("subscriptions/listen")},
                    {QStringLiteral("params"),
                     QJsonObject{{QStringLiteral("_meta"), modernMeta()},
                                 {QStringLiteral("notifications"),
                                  QJsonObject{{QStringLiteral("resourcesListChanged"), true},
                                              {QStringLiteral("resourceSubscriptions"),
                                               QJsonArray{QStringLiteral("test://static")}}}}}}});
    client.send(request("POST", body, {{"Mcp-Method", "subscriptions/listen"}, {"Mcp-Name", {}}}));
    ASSERT_TRUE(runUntil([&] { return server->subscriptionCount() == 1; }));
    for (int round = 0; round < 400 && server->subscriptionCount() == 1; ++round) {
        for (int i = 0; i < 200; ++i) {
            server->notifyResourceListChanged();
        }
        runFor(1ms);
    }
    EXPECT_EQ(server->subscriptionCount(), 0U);
    EXPECT_TRUE(runUntil([&] { return http->connectionCount() == 0; }));
}

TEST_F(HttpServerTest, ConnectionLimit) {
    Limits limits;
    limits.maxHttpConnections = 2;
    start(limits);
    Client first(port);
    Client second(port);
    ASSERT_TRUE(runUntil([&] { return http->connectionCount() == 2; }));
    Client third(port);
    auto refused = third.response();
    ASSERT_TRUE(refused.received);
    EXPECT_EQ(refused.status, 503);
    EXPECT_TRUE(third.waitClosed());
    first.send(echo());
    EXPECT_EQ(first.response().status, 200);
    first.close();
    ASSERT_TRUE(runUntil([&] { return http->connectionCount() == 1; }));
    Client fourth(port);
    fourth.send(echo());
    auto accepted = fourth.response();
    ASSERT_TRUE(accepted.received);
    EXPECT_EQ(accepted.status, 200);
}

TEST_F(HttpServerTest, PortInUse) {
    HttpServer other(*server, HttpServerOptions{.port = port, .token = token});
    auto result = other.listen();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::McpPortInUse);
    EXPECT_EQ(result.error().detail, std::to_string(port));
}

TEST_F(HttpServerTest, LegacySessionOverHttp) {
    const QByteArray init = serialize(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("initialize")},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("protocolVersion"), QStringLiteral("2025-11-25")},
                     {QStringLiteral("capabilities"), QJsonObject()},
                     {QStringLiteral("clientInfo"),
                      QJsonObject{{QStringLiteral("name"), QStringLiteral("cursor")},
                                  {QStringLiteral("version"), QStringLiteral("1")}}}}}});
    const std::map<QByteArray, QByteArray> legacy{
        {"MCP-Protocol-Version", {}}, {"Mcp-Method", {}}, {"Mcp-Name", {}}};
    Client client(port);
    client.send(request("POST", init, legacy));
    auto initialized = client.response();
    ASSERT_TRUE(initialized.received);
    const QByteArray session = initialized.header("mcp-session-id");
    ASSERT_EQ(session.size(), 32);

    auto withSession = legacy;
    withSession["Mcp-Session-Id"] = session;
    withSession["MCP-Protocol-Version"] = "2025-11-25";
    Client stream(port);
    auto getHeaders = withSession;
    getHeaders["Accept"] = "text/event-stream";
    getHeaders["Content-Length"] = {};
    getHeaders["Content-Type"] = {};
    stream.send(request("GET", "", getHeaders));
    ASSERT_TRUE(runUntil([&] { return stream.partial().received; }));
    EXPECT_EQ(stream.partial().header("content-type"), "text/event-stream");
    server->notifyResourceListChanged();
    ASSERT_TRUE(runUntil([&] { return stream.partial().events().size() == 1; }));

    auto noSse = getHeaders;
    noSse["Accept"] = "application/json";
    EXPECT_EQ(statusOf(request("GET", "", noSse)), 406);

    auto deleteHeaders = getHeaders;
    deleteHeaders["Accept"] = {};
    Client deleter(port);
    deleter.send(request("DELETE", "", deleteHeaders));
    auto deleted = deleter.response();
    ASSERT_TRUE(deleted.received);
    EXPECT_EQ(deleted.status, 200);
    auto ended = stream.response(); // the GET stream ends with the session
    ASSERT_TRUE(ended.received);
    deleter.send(request("DELETE", "", deleteHeaders));
    EXPECT_EQ(deleter.response().status, 404);
}

} // namespace
} // namespace hio::mcp
