// McpServer behaviour over time (plan/features/0013 "Tests: Protocol"): cancellation before and
// after start, stream close, progress throttling, the in-flight limit, subscriptions and their
// throttling, list_changed, session expiry, activity entries, stop(), a close while a stream
// starts (review R8), duplicate in-flight ids (R5) and the server's Offloader (ADR-0162).
// Request/response shapes are covered by the golden files (golden_test.cpp). Limits are shrunk
// instead of a fake clock.

#include "fake_tools.h"
#include "hio/core/error.h"
#include "hio/mcp/activity.h"
#include "hio/mcp/consent.h"
#include "hio/mcp/json_rpc.h"
#include "hio/mcp/limits.h"
#include "hio/mcp/offload.h"
#include "hio/mcp/protocol.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/reply_channel.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/schema.h"
#include "hio/mcp/server.h"
#include "hio/mcp/tool_call.h"
#include "mcp_test_support.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QtCore/qcborcommon.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace hio::mcp {
namespace {

using namespace std::chrono_literals;
using test::FakeConsentBroker;
using test::RecordingActivitySink;
using test::RecordingReplyChannel;
using test::runFor;
using test::runUntil;

QJsonObject modernMeta(const QJsonObject& extra = {}) {
    QJsonObject meta{
        {QStringLiteral("io.modelcontextprotocol/protocolVersion"), QStringLiteral("2026-07-28")},
        {QStringLiteral("io.modelcontextprotocol/clientCapabilities"), QJsonObject()},
        {QStringLiteral("io.modelcontextprotocol/clientInfo"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("tester")},
                     {QStringLiteral("version"), QStringLiteral("1")}}}};
    for (auto it = extra.begin(); it != extra.end(); ++it) {
        meta.insert(it.key(), it.value());
    }
    return meta;
}

QByteArray request(const QJsonValue& id, const QString& method, QJsonObject params = {},
                   bool modern = true, const QJsonObject& metaExtra = {}) {
    if (modern) {
        params.insert(QStringLiteral("_meta"), modernMeta(metaExtra));
    } else if (!metaExtra.isEmpty()) {
        params.insert(QStringLiteral("_meta"), metaExtra);
    }
    QJsonObject message{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                        {QStringLiteral("method"), method},
                        {QStringLiteral("params"), params}};
    if (!id.isUndefined()) {
        message.insert(QStringLiteral("id"), id);
    }
    return serialize(message);
}

QByteArray callTool(const QJsonValue& id, const QString& tool, const QJsonObject& args = {},
                    bool modern = true, const QJsonObject& metaExtra = {}) {
    return request(id, QStringLiteral("tools/call"),
                   QJsonObject{{QStringLiteral("name"), tool}, {QStringLiteral("arguments"), args}},
                   modern, metaExtra);
}

QByteArray initialize() {
    return request(1, QStringLiteral("initialize"),
                   QJsonObject{{QStringLiteral("protocolVersion"), QStringLiteral("2025-11-25")},
                               {QStringLiteral("capabilities"), QJsonObject()},
                               {QStringLiteral("clientInfo"),
                                QJsonObject{{QStringLiteral("name"), QStringLiteral("legacy")},
                                            {QStringLiteral("version"), QStringLiteral("1")}}}},
                   false);
}

QJsonObject result(const QJsonObject& message) {
    return message.value(QStringLiteral("result")).toObject();
}

std::string errorCode(const QJsonObject& response) {
    return result(response)
        .value(QStringLiteral("structuredContent"))
        .toObject()
        .value(QStringLiteral("error"))
        .toObject()
        .value(QStringLiteral("code"))
        .toString()
        .toStdString();
}

class ServerTest : public ::testing::Test {
public:
    void SetUp() override { makeServer(Limits{}); }

    void makeServer(const Limits& limits) {
        server.reset();
        ServerOptions options;
        options.limits = limits;
        options.version = QStringLiteral("test");
        server = std::make_unique<McpServer>(test::makeFakeRegistry(), consent, activity,
                                             std::move(options));
    }

    static TransportInfo stdio(std::uint64_t connection = 1) {
        TransportInfo info;
        info.kind = TransportKind::Stdio;
        info.connectionId = connection;
        return info;
    }

    static TransportInfo http(std::optional<std::string> session = std::nullopt) {
        TransportInfo info;
        info.kind = TransportKind::Http;
        info.connectionId = 99;
        info.sessionId = std::move(session);
        return info;
    }

    // A modern HTTP request's transport info with the mirrored headers.
    static TransportInfo httpModern(const char* method, const char* name = nullptr) {
        TransportInfo info = http();
        info.protocolVersionHeader = "2026-07-28";
        info.methodHeader = method;
        if (name != nullptr) {
            info.nameHeader = name;
        }
        return info;
    }

    // A legacy HTTP session; returns its id.
    [[nodiscard]] std::string legacyHttpSession() const {
        auto channel = std::make_shared<RecordingReplyChannel>();
        server->handle(initialize(), channel, http());
        EXPECT_TRUE(channel->meta.sessionId.has_value());
        return channel->meta.sessionId.value_or("");
    }

    FakeConsentBroker consent;
    RecordingActivitySink activity;
    std::unique_ptr<McpServer> server;
};

TEST_F(ServerTest, CancellationAfterStartSuppressesTheResponse) {
    auto channel = std::make_shared<RecordingReplyChannel>(true, true);
    server->handle(callTool(5, QStringLiteral("slow"), {{QStringLiteral("step_ms"), 5000}}),
                   channel, stdio());
    EXPECT_EQ(server->activeCalls(), 1U);
    server->handle(request(QJsonValue::Undefined, QStringLiteral("notifications/cancelled"),
                           {{QStringLiteral("requestId"), 5}}),
                   channel, stdio());
    EXPECT_EQ(server->activeCalls(), 0U);
    runFor(20ms);
    EXPECT_TRUE(channel->messages.empty());
    ASSERT_EQ(activity.entries.size(), 1U);
    EXPECT_EQ(activity.entries[0].result, "cancelled");
    EXPECT_EQ(activity.entries[0].tool, "slow");
}

TEST_F(ServerTest, CancellationIsScopedToTheConnection) {
    auto channel = std::make_shared<RecordingReplyChannel>(true, true);
    server->handle(callTool(5, QStringLiteral("slow"), {{QStringLiteral("step_ms"), 30}}), channel,
                   stdio(1));
    // Another stdio connection cancelling id 5, and an unknown id: both ignored.
    server->handle(request(QJsonValue::Undefined, QStringLiteral("notifications/cancelled"),
                           {{QStringLiteral("requestId"), 5}}),
                   std::make_shared<RecordingReplyChannel>(), stdio(2));
    server->handle(request(QJsonValue::Undefined, QStringLiteral("notifications/cancelled"),
                           {{QStringLiteral("requestId"), QStringLiteral("5")}}),
                   channel, stdio(1));
    ASSERT_TRUE(runUntil([&] { return !channel->messages.empty(); }));
    EXPECT_EQ(channel->messages.back().value(QStringLiteral("id")), QJsonValue(5));
}

TEST_F(ServerTest, CancellationBeforeStartWithdrawsTheConsentPrompt) {
    consent.mode = FakeConsentBroker::Mode::Manual;
    auto channel = std::make_shared<RecordingReplyChannel>(true, true);
    server->handle(initialize(), channel, stdio());
    server->handle(callTool(2, QStringLiteral("flash_fake"), {}, false), channel, stdio());
    ASSERT_EQ(consent.requests.size(), 1U);
    EXPECT_EQ(consent.requests[0].client.name, "legacy");
    EXPECT_EQ(consent.requests[0].categories, ConsentCategories(ConsentCategory::Flash));
    EXPECT_EQ(consent.requests[0].details.value(QStringLiteral("file")).toString(),
              QStringLiteral("app.hex"));
    server->handle(request(QJsonValue::Undefined, QStringLiteral("notifications/cancelled"),
                           {{QStringLiteral("requestId"), 2}}, false),
                   channel, stdio());
    EXPECT_TRUE(consent.withdrawn(0));
    runFor(10ms);
    EXPECT_EQ(channel->messages.size(), 1U); // only the initialize response
    // A cancellation for a request that is not running (any more) is ignored.
    server->handle(request(QJsonValue::Undefined, QStringLiteral("notifications/cancelled"),
                           {{QStringLiteral("requestId"), 3}}, false),
                   channel, stdio());
    server->handle(
        callTool(3, QStringLiteral("echo"), {{QStringLiteral("text"), QStringLiteral("x")}}, false),
        channel, stdio());
    EXPECT_EQ(channel->messages.size(), 2U);
}

TEST_F(ServerTest, ModernStreamCloseCancelsLegacyCloseDoesNot) {
    auto modern = std::make_shared<RecordingReplyChannel>();
    server->handle(callTool(1, QStringLiteral("slow"), {{QStringLiteral("step_ms"), 5000}}), modern,
                   httpModern("tools/call", "slow"));
    modern->close(CloseReason::PeerClosed);
    EXPECT_EQ(server->activeCalls(), 0U);
    ASSERT_EQ(activity.entries.size(), 1U);
    EXPECT_EQ(activity.entries[0].result, "cancelled");

    const std::string session = legacyHttpSession();
    auto legacy = std::make_shared<RecordingReplyChannel>();
    server->handle(callTool(2, QStringLiteral("slow"), {{QStringLiteral("step_ms"), 20}}, false),
                   legacy, http(session));
    legacy->close(CloseReason::PeerClosed);
    EXPECT_EQ(server->activeCalls(), 1U);
    ASSERT_TRUE(runUntil([&] { return server->activeCalls() == 0; }));
    EXPECT_EQ(activity.entries.back().result, "ok");

    // A slow client is dropped by the transport: its legacy calls are cancelled too.
    auto slow = std::make_shared<RecordingReplyChannel>();
    server->handle(callTool(3, QStringLiteral("slow"), {{QStringLiteral("step_ms"), 5000}}, false),
                   slow, http(session));
    slow->close(CloseReason::SlowClient);
    EXPECT_EQ(server->activeCalls(), 0U);
}

TEST_F(ServerTest, ProgressIsThrottledAndIncreasing) {
    Limits limits;
    limits.progressInterval = 100ms;
    makeServer(limits);
    auto channel = std::make_shared<RecordingReplyChannel>();
    server->handle(callTool(1, QStringLiteral("slow"),
                            {{QStringLiteral("step_ms"), 10}, {QStringLiteral("steps"), 40}}, true,
                            {{QStringLiteral("progressToken"), QStringLiteral("tok")}}),
                   channel, httpModern("tools/call", "slow"));
    EXPECT_TRUE(channel->streamStarted);
    ASSERT_TRUE(runUntil([&] { return channel->done(); }));
    // ~400 ms of progress at 100 ms intervals: the first at once, then about one per interval.
    EXPECT_GE(channel->notifications.size(), 2U);
    EXPECT_LE(channel->notifications.size(), 7U);
    double last = 0;
    for (const QJsonObject& notification : channel->notifications) {
        EXPECT_EQ(notification.value(QStringLiteral("method")).toString(),
                  QStringLiteral("notifications/progress"));
        const QJsonObject params = notification.value(QStringLiteral("params")).toObject();
        EXPECT_EQ(params.value(QStringLiteral("progressToken")).toString(), QStringLiteral("tok"));
        EXPECT_EQ(params.value(QStringLiteral("total")).toDouble(), 40.0);
        EXPECT_GT(params.value(QStringLiteral("progress")).toDouble(), last);
        last = params.value(QStringLiteral("progress")).toDouble();
    }
    EXPECT_TRUE(result(channel->response).value(QStringLiteral("isError")) == QJsonValue(false));

    // Without a progress token: no notifications, no stream.
    auto quiet = std::make_shared<RecordingReplyChannel>();
    server->handle(callTool(2, QStringLiteral("slow"),
                            {{QStringLiteral("step_ms"), 5}, {QStringLiteral("steps"), 5}}),
                   quiet, httpModern("tools/call", "slow"));
    ASSERT_TRUE(runUntil([&] { return quiet->done(); }));
    EXPECT_TRUE(quiet->notifications.empty());
    EXPECT_FALSE(quiet->streamStarted);
}

TEST_F(ServerTest, InFlightLimitGivesATooManyRequestsToolResult) {
    Limits limits;
    limits.maxInFlightCalls = 2;
    makeServer(limits);
    std::vector<std::shared_ptr<RecordingReplyChannel>> channels;
    for (int i = 0; i < 3; ++i) {
        channels.push_back(std::make_shared<RecordingReplyChannel>());
        server->handle(callTool(i, QStringLiteral("slow"), {{QStringLiteral("step_ms"), 5000}}),
                       channels.back(), httpModern("tools/call", "slow"));
    }
    EXPECT_EQ(server->activeCalls(), 2U);
    ASSERT_TRUE(channels[2]->done());
    EXPECT_TRUE(result(channels[2]->response).value(QStringLiteral("isError")).toBool());
    EXPECT_EQ(errorCode(channels[2]->response), "mcp.too_many_requests");
    EXPECT_EQ(activity.entries.back().result, "mcp.too_many_requests");
}

TEST_F(ServerTest, LegacySubscriptionUpdatesAreThrottledPerResource) {
    Limits limits;
    limits.resourceUpdateInterval = 150ms;
    makeServer(limits);
    auto channel = std::make_shared<RecordingReplyChannel>(true, true);
    server->handle(initialize(), channel, stdio());
    server->handle(request(2, QStringLiteral("resources/subscribe"),
                           {{QStringLiteral("uri"), QStringLiteral("test://static")}}, false),
                   channel, stdio());
    EXPECT_EQ(server->subscriptionCount(), 1U);
    channel->notifications.clear();
    for (int i = 0; i < 5; ++i) {
        server->notifyResourceUpdated(QStringLiteral("test://static"));
    }
    server->notifyResourceUpdated(QStringLiteral("test://item/a/status")); // not subscribed
    EXPECT_EQ(channel->notifications.size(), 1U);
    runFor(250ms);
    ASSERT_EQ(channel->notifications.size(), 2U); // one trailing notification
    for (const QJsonObject& notification : channel->notifications) {
        EXPECT_EQ(notification.value(QStringLiteral("method")).toString(),
                  QStringLiteral("notifications/resources/updated"));
        EXPECT_EQ(
            notification.value(QStringLiteral("params")).toObject().value(QStringLiteral("uri")),
            QJsonValue(QStringLiteral("test://static")));
    }
    server->notifyResourceListChanged();
    ASSERT_EQ(channel->notifications.size(), 3U);
    EXPECT_EQ(channel->notifications.back().value(QStringLiteral("method")).toString(),
              QStringLiteral("notifications/resources/list_changed"));

    server->handle(request(3, QStringLiteral("resources/unsubscribe"),
                           {{QStringLiteral("uri"), QStringLiteral("test://static")}}, false),
                   channel, stdio());
    EXPECT_EQ(server->subscriptionCount(), 0U);
    server->notifyResourceUpdated(QStringLiteral("test://static"));
    runFor(200ms);
    EXPECT_EQ(channel->notifications.size(), 3U);
}

TEST_F(ServerTest, ModernListenAcknowledgesAndDeliversWithSubscriptionId) {
    Limits limits;
    limits.maxSubscriptions = 2;
    limits.resourceUpdateInterval = 50ms;
    makeServer(limits);
    auto listen = std::make_shared<RecordingReplyChannel>();
    server->handle(request(QStringLiteral("L1"), QStringLiteral("subscriptions/listen"),
                           {{QStringLiteral("notifications"),
                             QJsonObject{{QStringLiteral("toolsListChanged"), true},
                                         {QStringLiteral("resourcesListChanged"), true},
                                         {QStringLiteral("resourceSubscriptions"),
                                          QJsonArray{QStringLiteral("test://static"),
                                                     QStringLiteral("test://nope"),
                                                     QStringLiteral("test://item/a/status"),
                                                     QStringLiteral("test://item/b/status")}}}}}),
                   listen, httpModern("subscriptions/listen"));
    EXPECT_TRUE(listen->streamStarted);
    EXPECT_FALSE(listen->done());
    ASSERT_EQ(listen->notifications.size(), 1U);
    const QJsonObject ack = listen->notifications[0];
    EXPECT_EQ(ack.value(QStringLiteral("method")).toString(),
              QStringLiteral("notifications/subscriptions/acknowledged"));
    const QJsonObject params = ack.value(QStringLiteral("params")).toObject();
    EXPECT_EQ(params.value(QStringLiteral("_meta"))
                  .toObject()
                  .value(QStringLiteral("io.modelcontextprotocol/subscriptionId")),
              QJsonValue(QStringLiteral("L1")));
    const QJsonObject honored = params.value(QStringLiteral("notifications")).toObject();
    EXPECT_FALSE(honored.contains(QStringLiteral("toolsListChanged")));
    EXPECT_TRUE(honored.value(QStringLiteral("resourcesListChanged")).toBool());
    EXPECT_EQ(
        honored.value(QStringLiteral("resourceSubscriptions")).toArray(),
        (QJsonArray{QStringLiteral("test://static"), QStringLiteral("test://item/a/status")}));
    EXPECT_EQ(server->subscriptionCount(), 2U);

    server->notifyResourceUpdated(QStringLiteral("test://item/a/status"));
    server->notifyResourceUpdated(QStringLiteral("test://item/b/status"));
    server->notifyResourceListChanged();
    ASSERT_EQ(listen->notifications.size(), 3U);
    EXPECT_EQ(listen->notifications[1]
                  .value(QStringLiteral("params"))
                  .toObject()
                  .value(QStringLiteral("_meta"))
                  .toObject()
                  .value(QStringLiteral("io.modelcontextprotocol/subscriptionId")),
              QJsonValue(QStringLiteral("L1")));
    EXPECT_EQ(listen->notifications[2].value(QStringLiteral("method")).toString(),
              QStringLiteral("notifications/resources/list_changed"));

    listen->close();
    EXPECT_EQ(server->subscriptionCount(), 0U);
}

TEST_F(ServerTest, ListenStreamsAreLimited) {
    Limits limits;
    limits.maxListenStreams = 1;
    makeServer(limits);
    const QJsonObject params{{QStringLiteral("notifications"),
                              QJsonObject{{QStringLiteral("resourcesListChanged"), true}}}};
    auto first = std::make_shared<RecordingReplyChannel>();
    server->handle(request(1, QStringLiteral("subscriptions/listen"), params), first,
                   httpModern("subscriptions/listen"));
    auto second = std::make_shared<RecordingReplyChannel>();
    server->handle(request(2, QStringLiteral("subscriptions/listen"), params), second,
                   httpModern("subscriptions/listen"));
    ASSERT_TRUE(second->done());
    EXPECT_EQ(second->meta.status, ReplyStatus::ServiceUnavailable);
    // A legacy GET stream counts too.
    const std::string session = legacyHttpSession();
    EXPECT_EQ(server->openSessionStream(http(session), std::make_shared<RecordingReplyChannel>()),
              ReplyStatus::ServiceUnavailable);
    first->close();
    EXPECT_EQ(server->openSessionStream(http(session), std::make_shared<RecordingReplyChannel>()),
              ReplyStatus::Ok);
}

TEST_F(ServerTest, StdioListenEndsWithCancelledNotification) {
    auto channel = std::make_shared<RecordingReplyChannel>(true, true);
    server->handle(request(7, QStringLiteral("subscriptions/listen"),
                           {{QStringLiteral("notifications"),
                             QJsonObject{{QStringLiteral("resourcesListChanged"), true}}}}),
                   channel, stdio());
    ASSERT_EQ(channel->notifications.size(), 1U);
    server->handle(request(QJsonValue::Undefined, QStringLiteral("notifications/cancelled"),
                           {{QStringLiteral("requestId"), 7}}),
                   channel, stdio());
    server->notifyResourceListChanged();
    EXPECT_EQ(channel->notifications.size(), 1U);
    EXPECT_EQ(channel->messages.size(), 1U); // no response for the cancelled listen
}

TEST_F(ServerTest, LegacyHttpSessionGetStreamReceivesNotifications) {
    const std::string session = legacyHttpSession();
    auto stream = std::make_shared<RecordingReplyChannel>();
    EXPECT_EQ(server->openSessionStream(http(session), stream), ReplyStatus::Ok);
    EXPECT_TRUE(stream->streamStarted);
    auto channel = std::make_shared<RecordingReplyChannel>();
    server->handle(request(2, QStringLiteral("resources/subscribe"),
                           {{QStringLiteral("uri"), QStringLiteral("test://static")}}, false),
                   channel, http(session));
    server->notifyResourceUpdated(QStringLiteral("test://static"));
    server->notifyResourceListChanged();
    ASSERT_EQ(stream->notifications.size(), 2U);

    // A second GET replaces the first stream (which ends).
    auto replacement = std::make_shared<RecordingReplyChannel>();
    EXPECT_EQ(server->openSessionStream(http(session), replacement), ReplyStatus::Ok);
    EXPECT_TRUE(stream->done());
    // DELETE ends the stream, the subscriptions and the consent memory.
    EXPECT_EQ(server->endSession(http(session)), ReplyStatus::Ok);
    EXPECT_TRUE(replacement->done());
    EXPECT_EQ(server->subscriptionCount(), 0U);
    ASSERT_FALSE(consent.gone.empty());
    EXPECT_EQ(consent.gone.back(), session);
}

TEST_F(ServerTest, LegacySessionsExpireAndAreLimited) {
    Limits limits;
    limits.legacySessionIdle = 100ms;
    limits.maxLegacySessions = 2;
    makeServer(limits);
    const std::string first = legacyHttpSession();
    const std::string second = legacyHttpSession();
    auto third = std::make_shared<RecordingReplyChannel>();
    server->handle(initialize(), third, http());
    EXPECT_EQ(third->meta.status, ReplyStatus::ServiceUnavailable);

    // Keep `second` alive; `first` expires.
    for (int i = 0; i < 6; ++i) {
        runFor(30ms);
        server->handle(request(10 + i, QStringLiteral("ping"), {}, false),
                       std::make_shared<RecordingReplyChannel>(), http(second));
    }
    ASSERT_TRUE(runUntil([&] { return server->legacySessionCount() == 1; }, 1000ms));
    auto expired = std::make_shared<RecordingReplyChannel>();
    server->handle(request(3, QStringLiteral("ping"), {}, false), expired, http(first));
    EXPECT_EQ(expired->meta.status, ReplyStatus::NotFound);
    EXPECT_NE(std::ranges::find(consent.gone, first), consent.gone.end());
    auto alive = std::make_shared<RecordingReplyChannel>();
    server->handle(request(4, QStringLiteral("ping"), {}, false), alive, http(second));
    EXPECT_EQ(alive->meta.status, ReplyStatus::Ok);
}

TEST_F(ServerTest, IdleModernClientsAreForgotten) {
    Limits limits;
    limits.modernClientIdle = 50ms;
    makeServer(limits);
    server->handle(request(1, QStringLiteral("tools/list")),
                   std::make_shared<RecordingReplyChannel>(), httpModern("tools/list"));
    ASSERT_TRUE(runUntil([&] { return !consent.gone.empty(); }, 1000ms));
    EXPECT_EQ(consent.gone.front(), "tester");
}

TEST_F(ServerTest, ActivityEntriesForEveryCall) {
    auto channel = std::make_shared<RecordingReplyChannel>(true, true);
    const QString longText(90, QLatin1Char('x'));
    server->handle(callTool(1, QStringLiteral("echo"), {{QStringLiteral("text"), longText}}),
                   channel, stdio());
    server->handle(callTool(2, QStringLiteral("fail"), {{QStringLiteral("device"), 1}}), channel,
                   stdio());
    server->handle(callTool(3, QStringLiteral("echo"),
                            {{QStringLiteral("device"), QStringLiteral("jlink:801000001")}}),
                   channel, stdio());
    server->handle(callTool(4, QStringLiteral("flash_fake")), channel, stdio());
    server->handle(callTool(5, QStringLiteral("no_consent")), channel, stdio());
    ASSERT_EQ(activity.entries.size(), 5U);
    const ActivityEntry& echo = activity.entries[0];
    EXPECT_EQ(echo.tool, "echo");
    EXPECT_EQ(echo.client, "tester");
    EXPECT_EQ(echo.clientKey, "tester");
    EXPECT_EQ(echo.dialect, Dialect::Modern);
    EXPECT_EQ(echo.result, "ok");
    EXPECT_LE(QString::fromStdString(echo.arguments).size(), 201);
    EXPECT_TRUE(QString::fromStdString(echo.arguments).contains(QChar(0x2026)));
    EXPECT_TRUE(echo.time.isValid());
    EXPECT_EQ(activity.entries[1].result, "rpc.invalid_params"); // "device" is not in the schema
    EXPECT_EQ(activity.entries[2].result, "rpc.invalid_params");
    EXPECT_EQ(activity.entries[2].deviceId, "jlink:801000001");
    EXPECT_EQ(activity.entries[3].result, "ok");
    EXPECT_EQ(activity.entries[3].deviceId, "dev1");
    EXPECT_TRUE(activity.entries[3].marker);
    EXPECT_EQ(activity.entries[4].result, "internal.unexpected");
}

TEST_F(ServerTest, ConsentDenialBecomesAToolError) {
    consent.mode = FakeConsentBroker::Mode::Deny;
    auto channel = std::make_shared<RecordingReplyChannel>();
    server->handle(callTool(1, QStringLiteral("flash_fake")), channel,
                   httpModern("tools/call", "flash_fake"));
    ASSERT_TRUE(channel->done());
    EXPECT_EQ(errorCode(channel->response), "mcp.tool_not_allowed");
    EXPECT_EQ(activity.entries.back().result, "mcp.tool_not_allowed");
}

TEST_F(ServerTest, StopCancelsCallsEndsStreamsAndRefusesNewMessages) {
    auto call = std::make_shared<RecordingReplyChannel>();
    server->handle(callTool(1, QStringLiteral("slow"), {{QStringLiteral("step_ms"), 5000}}), call,
                   httpModern("tools/call", "slow"));
    auto listen = std::make_shared<RecordingReplyChannel>();
    server->handle(request(QStringLiteral("L"), QStringLiteral("subscriptions/listen"),
                           {{QStringLiteral("notifications"), QJsonObject()}}),
                   listen, httpModern("subscriptions/listen"));
    const std::string session = legacyHttpSession();
    bool drained = false;
    QObject::connect(server.get(), &McpServer::callsDrained, [&drained] { drained = true; });
    server->stop();
    EXPECT_TRUE(drained);
    EXPECT_TRUE(server->isStopped());
    ASSERT_TRUE(call->done());
    EXPECT_EQ(errorCode(call->response), "cancelled");
    ASSERT_TRUE(listen->done());
    const QJsonObject listenResult = result(listen->response);
    EXPECT_EQ(listenResult.value(QStringLiteral("resultType")).toString(),
              QStringLiteral("complete"));
    EXPECT_EQ(listenResult.value(QStringLiteral("_meta"))
                  .toObject()
                  .value(QStringLiteral("io.modelcontextprotocol/subscriptionId")),
              QJsonValue(QStringLiteral("L")));
    EXPECT_EQ(server->legacySessionCount(), 0U);
    EXPECT_NE(std::ranges::find(consent.gone, session), consent.gone.end());

    auto late = std::make_shared<RecordingReplyChannel>();
    server->handle(request(9, QStringLiteral("tools/list")), late, httpModern("tools/list"));
    ASSERT_TRUE(late->done());
    EXPECT_EQ(
        late->response.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")),
        QJsonValue(protocol::errc::internalError));
}

TEST_F(ServerTest, StdioReinitializeReplacesTheSession) {
    auto channel = std::make_shared<RecordingReplyChannel>(true, true);
    server->handle(initialize(), channel, stdio());
    server->handle(initialize(), channel, stdio());
    EXPECT_EQ(server->legacySessionCount(), 1U);
    EXPECT_EQ(consent.gone.size(), 1U);
}

// A channel whose peer goes away while the stream head is written (review R8).
class ClosingOnStreamChannel final : public IReplyChannel {
public:
    bool closed = false;
    int notifications = 0;
    int completions = 0;
    std::function<void(CloseReason)> closedHandler;

    [[nodiscard]] bool canStream() const override { return true; }
    void startStream() override {
        closed = true;
        if (auto handler = std::exchange(closedHandler, {})) {
            handler(CloseReason::SlowClient);
        }
    }
    void notify(const QJsonObject& /*notification*/) override {
        if (!closed) {
            ++notifications;
        }
    }
    void complete(const QJsonObject& /*response*/, const ReplyMeta& /*meta*/) override {
        if (!closed) {
            ++completions;
        }
    }
    [[nodiscard]] bool isOpen() const override { return !closed; }
    void setClosedHandler(std::function<void(CloseReason)> handler) override {
        closedHandler = std::move(handler);
    }
};

TEST_F(ServerTest, CloseWhileTheStreamStartsIsSeen) {
    Limits limits;
    limits.maxListenStreams = 1;
    makeServer(limits);
    // A listen stream: the subscription is gone again.
    auto listen = std::make_shared<ClosingOnStreamChannel>();
    server->handle(request(1, QStringLiteral("subscriptions/listen"),
                           {{QStringLiteral("notifications"),
                             QJsonObject{{QStringLiteral("resourceSubscriptions"),
                                          QJsonArray{QStringLiteral("test://static")}}}}}),
                   listen, httpModern("subscriptions/listen"));
    EXPECT_EQ(server->subscriptionCount(), 0U);
    EXPECT_EQ(listen->completions, 0);

    // A tool call with a progress token (stream at once): cancelled.
    auto call = std::make_shared<ClosingOnStreamChannel>();
    server->handle(callTool(2, QStringLiteral("slow"), {{QStringLiteral("step_ms"), 5000}}, true,
                            {{QStringLiteral("progressToken"), 1}}),
                   call, httpModern("tools/call", "slow"));
    EXPECT_EQ(server->activeCalls(), 0U);
    ASSERT_FALSE(activity.entries.empty());
    EXPECT_EQ(activity.entries.back().result, "cancelled");

    // A legacy GET stream: the slot is free again.
    const std::string session = legacyHttpSession();
    EXPECT_EQ(server->openSessionStream(http(session), std::make_shared<ClosingOnStreamChannel>()),
              ReplyStatus::Ok);
    auto next = std::make_shared<RecordingReplyChannel>();
    server->handle(request(3, QStringLiteral("subscriptions/listen"),
                           {{QStringLiteral("notifications"), QJsonObject()}}),
                   next, httpModern("subscriptions/listen"));
    EXPECT_TRUE(next->streamStarted);
    EXPECT_FALSE(next->done());
}

int rpcErrorCode(const QJsonObject& message) {
    return message.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt();
}

TEST_F(ServerTest, ModernDuplicateIdInFlightIsRefused) {
    auto channel = std::make_shared<RecordingReplyChannel>(true, true);
    server->handle(callTool(5, QStringLiteral("slow"), {{QStringLiteral("step_ms"), 5000}}),
                   channel, stdio(1));
    server->handle(
        callTool(5, QStringLiteral("echo"), {{QStringLiteral("text"), QStringLiteral("x")}}),
        channel, stdio(1));
    ASSERT_EQ(channel->messages.size(), 1U);
    EXPECT_EQ(rpcErrorCode(channel->messages.back()), protocol::errc::invalidRequest);
    EXPECT_TRUE(channel->messages.back()
                    .value(QStringLiteral("error"))
                    .toObject()
                    .value(QStringLiteral("message"))
                    .toString()
                    .contains(QStringLiteral("duplicate id")));
    EXPECT_EQ(channel->messages.back().value(QStringLiteral("id")), QJsonValue(5));
    EXPECT_EQ(activity.entries.back().result, "rpc.invalid_request");
    EXPECT_EQ(server->activeCalls(), 1U);

    // The same id on another connection, or a string id "5", is another request.
    auto other = std::make_shared<RecordingReplyChannel>(true, true);
    server->handle(callTool(5, QStringLiteral("slow"), {{QStringLiteral("step_ms"), 5000}}), other,
                   stdio(2));
    server->handle(
        callTool(QStringLiteral("5"), QStringLiteral("slow"), {{QStringLiteral("step_ms"), 5000}}),
        channel, stdio(1));
    EXPECT_EQ(server->activeCalls(), 3U);
    EXPECT_TRUE(other->messages.empty());

    // A listen stream's id is in flight too.
    server->handle(request(QStringLiteral("L"), QStringLiteral("subscriptions/listen"),
                           {{QStringLiteral("notifications"), QJsonObject()}}),
                   channel, stdio(1));
    const std::size_t before = channel->messages.size();
    server->handle(callTool(QStringLiteral("L"), QStringLiteral("echo"),
                            {{QStringLiteral("text"), QStringLiteral("x")}}),
                   channel, stdio(1));
    ASSERT_EQ(channel->messages.size(), before + 1);
    EXPECT_EQ(rpcErrorCode(channel->messages.back()), protocol::errc::invalidRequest);
    server->handle(request(QStringLiteral("L"), QStringLiteral("subscriptions/listen"),
                           {{QStringLiteral("notifications"), QJsonObject()}}),
                   channel, stdio(1));
    ASSERT_EQ(channel->messages.size(), before + 2);
    EXPECT_EQ(rpcErrorCode(channel->messages.back()), protocol::errc::invalidRequest);

    // After the cancellation the id may be used again.
    server->handle(request(QJsonValue::Undefined, QStringLiteral("notifications/cancelled"),
                           {{QStringLiteral("requestId"), 5}}),
                   channel, stdio(1));
    server->handle(
        callTool(5, QStringLiteral("echo"), {{QStringLiteral("text"), QStringLiteral("y")}}),
        channel, stdio(1));
    ASSERT_EQ(channel->messages.size(), before + 3);
    EXPECT_FALSE(channel->messages.back().contains(QStringLiteral("error")));
}

TEST_F(ServerTest, LegacyDuplicateIdInFlightIsRefused) {
    auto channel = std::make_shared<RecordingReplyChannel>(true, true);
    server->handle(initialize(), channel, stdio(1));
    server->handle(callTool(2, QStringLiteral("slow"), {{QStringLiteral("step_ms"), 5000}}, false),
                   channel, stdio(1));
    server->handle(
        callTool(2, QStringLiteral("echo"), {{QStringLiteral("text"), QStringLiteral("x")}}, false),
        channel, stdio(1));
    ASSERT_EQ(channel->messages.size(), 2U);
    EXPECT_EQ(rpcErrorCode(channel->messages.back()), protocol::errc::invalidRequest);
    EXPECT_EQ(server->activeCalls(), 1U);

    // Legacy over HTTP: the scope is the session, not the connection.
    const std::string session = legacyHttpSession();
    auto first = std::make_shared<RecordingReplyChannel>();
    server->handle(callTool(7, QStringLiteral("slow"), {{QStringLiteral("step_ms"), 5000}}, false),
                   first, http(session));
    first->close(); // a legacy disconnect is no cancellation: the call goes on
    auto again = std::make_shared<RecordingReplyChannel>();
    server->handle(
        callTool(7, QStringLiteral("echo"), {{QStringLiteral("text"), QStringLiteral("x")}}, false),
        again, http(session));
    ASSERT_TRUE(again->done());
    EXPECT_EQ(rpcErrorCode(again->response), protocol::errc::invalidRequest);
    // Another session may use the same id.
    const std::string otherSession = legacyHttpSession();
    auto other = std::make_shared<RecordingReplyChannel>();
    server->handle(
        callTool(7, QStringLiteral("echo"), {{QStringLiteral("text"), QStringLiteral("x")}}, false),
        other, http(otherSession));
    ASSERT_TRUE(other->done());
    EXPECT_FALSE(other->response.contains(QStringLiteral("error")));
}

TEST_F(ServerTest, OwnsTheOffloaderAndStopJoinsItsJobs) {
    Limits limits;
    limits.maxOffloadRunning = 1;
    limits.maxOffloadQueued = 1;
    // A tool that reports the environment's offloader.
    Offloader* seen = nullptr;
    Registry registry;
    ASSERT_TRUE(
        registry
            .addTool(ToolSpec{.name = "probe",
                              .title = {},
                              .description = "Reports the offloader.",
                              .inputSchema = Schema::object().json(),
                              .outputSchema = {},
                              .annotations = {},
                              .consent = {},
                              .marker = false,
                              .start =
                                  [&seen](const RequestContext&, const QJsonObject&, ToolEnv& env) {
                                      seen = &env.offloader();
                                      return ToolCall::completed(toolResult(QJsonObject()));
                                  },
                              .validate = {}})
            .has_value());
    ServerOptions options;
    options.limits = limits;
    options.version = QStringLiteral("test");
    server =
        std::make_unique<McpServer>(std::move(registry), consent, activity, std::move(options));
    server->handle(callTool(1, QStringLiteral("probe")), std::make_shared<RecordingReplyChannel>(),
                   httpModern("tools/call", "probe"));
    EXPECT_EQ(seen, &server->offloader());

    auto stopped = std::make_shared<std::atomic<int>>(0);
    const auto job = [stopped](const std::stop_token& token) {
        while (!token.stop_requested()) {
            std::this_thread::sleep_for(1ms);
        }
        ++*stopped;
        return 0;
    };
    QObject receiver;
    bool called = false;
    auto running =
        server->offloader().submit<int>(receiver, job, [&called](int) { called = true; });
    auto queued = server->offloader().submit<int>(receiver, job, [&called](int) { called = true; });
    auto refused = server->offloader().submit<int>(receiver, job, [](int) {});
    ASSERT_TRUE(running.has_value());
    ASSERT_TRUE(queued.has_value());
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::McpTooManyRequests);
    server->stop();
    EXPECT_EQ(stopped->load(), 1); // joined; the queued job never ran
    EXPECT_EQ(server->offloader().running(), 0U);
    EXPECT_EQ(server->offloader().queued(), 0U);
    runFor(20ms);
    EXPECT_FALSE(called);
}

} // namespace
} // namespace hio::mcp
