// Resources and the debug_device prompt (plan/features/0013 C3, ADR-0107): hio://devices,
// hio://device/{id}/status|log|terminal, resources/list with the open devices, list_changed on
// open/close, resources/updated on new lines; the prompt text.

#include "hio/device/console_line.h"
#include "hio/mcp/limits.h"
#include "hio/mcp/request_context.h"
#include "mcp_test_support.h"
#include "tools_test_support.h"

#include <QChar>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>

namespace hio::mcp::tools::test {
namespace {

using namespace std::chrono_literals;

QJsonObject resultOf(const QJsonObject& response) {
    return response.value(QStringLiteral("result")).toObject();
}

QString readText(Harness& harness, const QString& uri) {
    const QJsonObject result =
        resultOf(harness.rpc(QStringLiteral("resources/read"), {{QStringLiteral("uri"), uri}}));
    return result.value(QStringLiteral("contents"))
        .toArray()
        .at(0)
        .toObject()
        .value(QStringLiteral("text"))
        .toString();
}

TEST(Resources, ListAndRead) {
    FakeProvider provider;
    Harness harness(provider);
    auto& session = provider.add("jlink:801000001");
    session.shell("uart:~$ hello");
    session.emitLine(hio::device::Channel::Shell, hio::device::Direction::In, "version");
    session.logLine("<inf> app: started");

    const QJsonArray resources = resultOf(harness.rpc(QStringLiteral("resources/list")))
                                     .value(QStringLiteral("resources"))
                                     .toArray();
    QList<QString> uris;
    for (const auto& item : resources) {
        uris.append(item.toObject().value(QStringLiteral("uri")).toString());
    }
    EXPECT_TRUE(uris.contains(QStringLiteral("hio://devices")));
    EXPECT_TRUE(uris.contains(QStringLiteral("hio://device/jlink:801000001/status")));
    EXPECT_TRUE(uris.contains(QStringLiteral("hio://device/jlink:801000001/log")));
    EXPECT_TRUE(uris.contains(QStringLiteral("hio://device/jlink:801000001/terminal")));

    const QJsonObject devices =
        QJsonDocument::fromJson(readText(harness, QStringLiteral("hio://devices")).toUtf8())
            .object();
    EXPECT_EQ(devices.value(QStringLiteral("devices")).toArray().size(), 1);
    const QJsonObject status =
        QJsonDocument::fromJson(
            readText(harness, QStringLiteral("hio://device/jlink:801000001/status")).toUtf8())
            .object();
    EXPECT_EQ(status.value(QStringLiteral("terminal_total")).toInteger(), 2);
    EXPECT_EQ(readText(harness, QStringLiteral("hio://device/jlink:801000001/terminal")),
              QStringLiteral("uart:~$ hello\n> version"));
    EXPECT_EQ(readText(harness, QStringLiteral("hio://device/jlink:801000001/log")),
              QStringLiteral("<inf> app: started"));

    const QJsonObject missing =
        harness.rpc(QStringLiteral("resources/read"),
                    {{QStringLiteral("uri"), QStringLiteral("hio://device/jlink:9/log")}});
    EXPECT_TRUE(missing.contains(QStringLiteral("error")));
}

TEST(Resources, TerminalResourceKeepsTheLast1000Lines) {
    FakeProvider provider;
    Harness harness(provider);
    auto& session = provider.add("jlink:1");
    for (int i = 0; i < 1100; ++i) {
        session.shell("line " + std::to_string(i));
    }
    const QString text = readText(harness, QStringLiteral("hio://device/jlink:1/terminal"));
    const auto lines = text.split(QLatin1Char('\n'));
    ASSERT_EQ(lines.size(), 1000);
    EXPECT_EQ(lines.front(), QStringLiteral("line 100"));
}

TEST(Resources, ListChangedAndUpdatedNotifications) {
    FakeProvider provider;
    hio::mcp::Limits limits;
    limits.resourceUpdateInterval = 20ms;
    Harness harness(provider, limits);
    auto& first = provider.add("jlink:1");

    auto listen = std::make_shared<hio::mcp::test::RecordingReplyChannel>();
    hio::mcp::TransportInfo http;
    http.kind = hio::mcp::TransportKind::Http;
    http.connectionId = 99;
    http.protocolVersionHeader = "2026-07-28";
    http.methodHeader = "subscriptions/listen";
    harness.server->handle(
        request(QStringLiteral("L1"), QStringLiteral("subscriptions/listen"),
                {{QStringLiteral("notifications"),
                  QJsonObject{{QStringLiteral("resourcesListChanged"), true},
                              {QStringLiteral("resourceSubscriptions"),
                               QJsonArray{QStringLiteral("hio://device/jlink:1/log"),
                                          QStringLiteral("hio://devices")}}}}}),
        listen, http);
    ASSERT_TRUE(listen->streamStarted);
    const auto count = [&listen](const char* method) {
        int n = 0;
        for (const QJsonObject& message : listen->notifications) {
            n += message.value(QStringLiteral("method")).toString() == QString::fromLatin1(method)
                     ? 1
                     : 0;
        }
        return n;
    };

    first.logLine("<inf> new");
    ASSERT_TRUE(runUntil([&] { return count("notifications/resources/updated") >= 1; }, 2000ms));

    provider.add("jlink:2");
    ASSERT_TRUE(
        runUntil([&] { return count("notifications/resources/list_changed") >= 1; }, 2000ms));
    provider.remove("jlink:2");
    ASSERT_TRUE(
        runUntil([&] { return count("notifications/resources/list_changed") >= 2; }, 2000ms));
    listen->close();
}

TEST(Prompts, DebugDevice) {
    FakeProvider provider;
    Harness harness(provider);
    const QJsonObject list = resultOf(harness.rpc(QStringLiteral("prompts/list")));
    ASSERT_EQ(list.value(QStringLiteral("prompts")).toArray().size(), 1);
    const QJsonObject prompt = resultOf(harness.rpc(
        QStringLiteral("prompts/get"), {{QStringLiteral("name"), QStringLiteral("debug_device")}}));
    const QString text = prompt.value(QStringLiteral("messages"))
                             .toArray()
                             .at(0)
                             .toObject()
                             .value(QStringLiteral("content"))
                             .toObject()
                             .value(QStringLiteral("text"))
                             .toString();
    EXPECT_TRUE(text.contains(QStringLiteral("jlink_close -> release_probe")));
    EXPECT_TRUE(text.contains(QStringLiteral("jlink_open -> reacquire_probe")));
    EXPECT_TRUE(text.contains(QStringLiteral("\"lte\"")));
    EXPECT_TRUE(text.contains(QStringLiteral("mcp.tool_not_allowed")));
    EXPECT_FALSE(text.contains(QStringLiteral("/upload")));
}

} // namespace
} // namespace hio::mcp::tools::test
