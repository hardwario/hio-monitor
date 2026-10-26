// Golden JSON exchanges of McpServer in both dialects (plan/features/0013 "Tests: Protocol").
//
// Every golden/*.json file is one scenario on a fresh server with the fake tool set
// (fake_tools.h), ServerOptions::version "test" and an allowing consent broker:
//   {"description": "...", "steps": [step, ...]}
// step:
//   "transport": "stdio" (default) | "http"
//   "connection": stdio/HTTP connection id (default 1); stdio connections keep one channel
//   "headers": {"MCP-Protocol-Version"|"Mcp-Method"|"Mcp-Name"|"Mcp-Session-Id": value}
//              ("$session" = the session id captured before)
//   "accept": "both" (default) | "json" (HTTP channel that cannot stream)
//   "send": message object | "sendRaw": string | "action": "delete" | "get"
//   "expect": [messages] - every message the server sends for this step, in order
//   "status": "ok" | "accepted" | "bad_request" | "not_found" | "service_unavailable" (HTTP)
//   "capture": true - remember the Mcp-Session-Id of the response as "$session"
//   "waitMs": how long to wait for messages when the step does not complete (default 100)
// In expectations the string "*" matches any value and "$session" the captured session id.
//
// HIO_MCP_UPDATE_GOLDEN=1 rewrites the "expect"/"status" members with the actual output (review
// the diff before committing).

#include "fake_tools.h"
#include "hio/mcp/json_rpc.h"
#include "hio/mcp/reply_channel.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/server.h"
#include "mcp_test_support.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QtContainerFwd>
#include <QtCore/qcborcommon.h>
#include <QtEnvironmentVariables>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hio::mcp {
namespace {

using test::FakeConsentBroker;
using test::RecordingActivitySink;
using test::RecordingReplyChannel;

const QString anyValue = QStringLiteral("*");
const QString sessionPlaceholder = QStringLiteral("$session");

bool matches(const QJsonValue& expected, const QJsonValue& actual, const QString& session) {
    if (expected.isString() && expected.toString() == anyValue) {
        return true;
    }
    if (expected.isString() && expected.toString() == sessionPlaceholder) {
        return actual.isString() && actual.toString() == session;
    }
    if (expected.isObject()) {
        if (!actual.isObject()) {
            return false;
        }
        const QJsonObject e = expected.toObject();
        const QJsonObject a = actual.toObject();
        if (e.keys() != a.keys()) {
            return false;
        }
        for (auto it = e.constBegin(); it != e.constEnd(); ++it) {
            if (!matches(it.value(), a.value(it.key()), session)) {
                return false;
            }
        }
        return true;
    }
    if (expected.isArray()) {
        if (!actual.isArray() || expected.toArray().size() != actual.toArray().size()) {
            return false;
        }
        for (qsizetype i = 0; i < expected.toArray().size(); ++i) {
            if (!matches(expected.toArray().at(i), actual.toArray().at(i), session)) {
                return false;
            }
        }
        return true;
    }
    return expected == actual;
}

QJsonValue withPlaceholders(const QJsonValue& value, const QString& session) {
    if (value.isString() && !session.isEmpty() && value.toString() == session) {
        return sessionPlaceholder;
    }
    if (value.isObject()) {
        QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            it.value() = withPlaceholders(it.value(), session);
        }
        return object;
    }
    if (value.isArray()) {
        QJsonArray array = value.toArray();
        for (qsizetype i = 0; i < array.size(); ++i) {
            array.replace(i, withPlaceholders(array.at(i), session));
        }
        return array;
    }
    return value;
}

QString statusName(ReplyStatus status) {
    switch (status) {
    case ReplyStatus::Ok:
        return QStringLiteral("ok");
    case ReplyStatus::Accepted:
        return QStringLiteral("accepted");
    case ReplyStatus::BadRequest:
        return QStringLiteral("bad_request");
    case ReplyStatus::NotFound:
        return QStringLiteral("not_found");
    case ReplyStatus::ServiceUnavailable:
        return QStringLiteral("service_unavailable");
    }
    return {};
}

QString pretty(const QJsonValue& value) {
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented));
    }
    return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented));
}

// The outcome of one step: the messages and, for HTTP steps and actions, the status.
struct StepOutcome {
    QJsonArray messages;
    QString status; // statusName(); empty when the step has none
};

// Runs the steps of one scenario on a fresh server.
class ScenarioRunner {
public:
    ScenarioRunner() {
        ServerOptions options;
        options.version = QStringLiteral("test");
        m_server = std::make_unique<McpServer>(test::makeFakeRegistry(), m_consent, m_activity,
                                               std::move(options));
    }

    [[nodiscard]] const QString& session() const noexcept { return m_session; }

    StepOutcome run(const QJsonObject& step) {
        const bool http = step.value(QStringLiteral("transport")).toString() == "http";
        const auto connection =
            static_cast<std::uint64_t>(step.value(QStringLiteral("connection")).toInt(1));
        const TransportInfo transport = transportFor(step, http, connection);
        const std::shared_ptr<RecordingReplyChannel> channel = channelFor(step, http, connection);
        const std::size_t before = http ? 0 : channel->messages.size();
        const std::size_t completionsBefore = channel->completions;

        const QString action = step.value(QStringLiteral("action")).toString();
        QString actionStatus;
        if (action == QStringLiteral("delete")) {
            actionStatus = statusName(m_server->endSession(transport));
        } else if (action == QStringLiteral("get")) {
            actionStatus = statusName(m_server->openSessionStream(transport, channel));
        } else if (step.contains(QStringLiteral("sendRaw"))) {
            m_server->handle(step.value(QStringLiteral("sendRaw")).toString().toUtf8(), channel,
                             transport);
        } else {
            m_server->handle(serialize(step.value(QStringLiteral("send")).toObject()), channel,
                             transport);
        }

        const auto expected =
            static_cast<std::size_t>(step.value(QStringLiteral("expect")).toArray().size());
        test::runUntil(
            [&] {
                if (http) {
                    return channel->completions > completionsBefore;
                }
                return expected > 0 && channel->messages.size() - before >= expected;
            },
            std::chrono::milliseconds{step.value(QStringLiteral("waitMs")).toInt(100)});
        test::runFor(std::chrono::milliseconds{5});

        StepOutcome outcome;
        for (std::size_t i = before; i < channel->messages.size(); ++i) {
            outcome.messages.append(channel->messages.at(i));
        }
        outcome.status = actionStatus;
        if (outcome.status.isEmpty() && http && channel->done()) {
            outcome.status = statusName(channel->meta.status);
        }
        if (step.value(QStringLiteral("capture")).toBool() && channel->meta.sessionId) {
            m_session = QString::fromStdString(channel->meta.sessionId.value_or(""));
        }
        return outcome;
    }

private:
    TransportInfo transportFor(const QJsonObject& step, bool http, std::uint64_t connection) {
        if (!m_connectionIds.contains(connection)) {
            m_connectionIds[connection] = nextConnectionId();
        }
        TransportInfo transport;
        transport.kind = http ? TransportKind::Http : TransportKind::Stdio;
        transport.connectionId = m_connectionIds[connection];
        const QJsonObject headers = step.value(QStringLiteral("headers")).toObject();
        const auto header = [this, &headers](const char* name) -> std::optional<std::string> {
            if (!headers.contains(QLatin1StringView(name))) {
                return std::nullopt;
            }
            QString value = headers.value(QLatin1StringView(name)).toString();
            if (value == sessionPlaceholder) {
                value = m_session;
            }
            return value.toStdString();
        };
        transport.protocolVersionHeader = header("MCP-Protocol-Version");
        transport.methodHeader = header("Mcp-Method");
        transport.nameHeader = header("Mcp-Name");
        transport.sessionId = header("Mcp-Session-Id");
        return transport;
    }

    std::shared_ptr<RecordingReplyChannel> channelFor(const QJsonObject& step, bool http,
                                                      std::uint64_t connection) {
        if (http) {
            return std::make_shared<RecordingReplyChannel>(
                step.value(QStringLiteral("accept")).toString() != QStringLiteral("json"));
        }
        auto& persistent = m_stdioChannels[connection];
        if (!persistent) {
            persistent = std::make_shared<RecordingReplyChannel>(true, true);
        }
        return persistent;
    }

    FakeConsentBroker m_consent;
    RecordingActivitySink m_activity;
    std::unique_ptr<McpServer> m_server;
    std::map<std::uint64_t, std::shared_ptr<RecordingReplyChannel>> m_stdioChannels;
    std::map<std::uint64_t, std::uint64_t> m_connectionIds; // scenario id -> unique id
    QString m_session;
};

class GoldenTest : public ::testing::TestWithParam<QString> {};

TEST_P(GoldenTest, Exchange) {
    const QString path = QStringLiteral(HIO_MCP_GOLDEN_DIR) + QLatin1Char('/') + GetParam();
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly)) << path.toStdString();
    QJsonObject scenario = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    QJsonArray steps = scenario.value(QStringLiteral("steps")).toArray();
    ASSERT_FALSE(steps.isEmpty()) << path.toStdString();
    const bool update = qEnvironmentVariableIsSet("HIO_MCP_UPDATE_GOLDEN");

    ScenarioRunner runner;
    for (qsizetype index = 0; index < steps.size(); ++index) {
        QJsonObject step = steps.at(index).toObject();
        SCOPED_TRACE("step " + std::to_string(index) + " of " + GetParam().toStdString());
        const StepOutcome outcome = runner.run(step);
        if (update) {
            step.insert(QStringLiteral("expect"),
                        withPlaceholders(outcome.messages, runner.session()));
            if (!outcome.status.isEmpty()) {
                step.insert(QStringLiteral("status"), outcome.status);
            }
            steps.replace(index, step);
            continue;
        }
        const QJsonArray expected = step.value(QStringLiteral("expect")).toArray();
        EXPECT_TRUE(matches(expected, outcome.messages, runner.session()))
            << "expected:\n"
            << pretty(expected).toStdString() << "actual:\n"
            << pretty(outcome.messages).toStdString();
        if (step.contains(QStringLiteral("status"))) {
            EXPECT_EQ(outcome.status, step.value(QStringLiteral("status")).toString());
        }
    }

    if (update) {
        scenario.insert(QStringLiteral("steps"), steps);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(scenario).toJson(QJsonDocument::Indented));
    }
}

QStringList goldenFiles() {
    return QDir(QStringLiteral(HIO_MCP_GOLDEN_DIR))
        .entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
}

INSTANTIATE_TEST_SUITE_P(Golden, GoldenTest, ::testing::ValuesIn(goldenFiles()),
                         [](const ::testing::TestParamInfo<QString>& info) {
                             QString name = info.param;
                             name.chop(5);
                             return name.replace(QLatin1Char('-'), QLatin1Char('_')).toStdString();
                         });

} // namespace
} // namespace hio::mcp
