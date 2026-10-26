// StdioTransport with ILineSource (plan/features/0013 C4, ADR-0154): framing, overlong lines,
// end of input, a reader thread posting wake-ups (TSan), output failures.

#include "fake_tools.h"
#include "hio/mcp/json_rpc.h"
#include "hio/mcp/limits.h"
#include "hio/mcp/server.h"
#include "hio/mcp/stdio_transport.h"
#include "mcp_test_support.h"

#include <QBuffer>
#include <QByteArray>
#include <QByteArrayList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QStringLiteral>
#include <QtContainerFwd>
#include <QtCore/qhashfunctions.h>

#include <gtest/gtest.h>

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace hio::mcp {
namespace {

using namespace std::chrono_literals;

// An ILineSource whose lines are pushed by the test or by a reader thread. The wake-up is called
// under the mutex, so setWakeup({}) is serialized with it (the ILineSource contract).
class TestLineSource final : public ILineSource {
public:
    void push(QByteArray text, bool overlong = false) {
        const std::scoped_lock lock(m_mutex);
        m_lines.push_back(Line{.text = std::move(text), .overlong = overlong});
        if (m_wakeup) {
            m_wakeup();
        }
    }
    void end() {
        const std::scoped_lock lock(m_mutex);
        m_ended = true;
        if (m_wakeup) {
            m_wakeup();
        }
    }
    std::optional<Line> takeLine() override {
        const std::scoped_lock lock(m_mutex);
        if (m_lines.empty()) {
            return std::nullopt;
        }
        Line line = std::move(m_lines.front());
        m_lines.pop_front();
        return line;
    }
    bool atEnd() const override {
        const std::scoped_lock lock(m_mutex);
        return m_ended && m_lines.empty();
    }
    void setWakeup(std::function<void()> wakeup) override {
        const std::scoped_lock lock(m_mutex);
        m_wakeup = std::move(wakeup);
    }

private:
    mutable std::mutex m_mutex;
    std::deque<Line> m_lines;
    bool m_ended = false;
    std::function<void()> m_wakeup;
};

QByteArray echoLine(int id) {
    return serialize(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("method"), QStringLiteral("tools/call")},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("echo")},
                     {QStringLiteral("arguments"),
                      QJsonObject{{QStringLiteral("text"), QStringLiteral("x")}}},
                     {QStringLiteral("_meta"),
                      QJsonObject{{QStringLiteral("io.modelcontextprotocol/protocolVersion"),
                                   QStringLiteral("2026-07-28")},
                                  {QStringLiteral("io.modelcontextprotocol/clientCapabilities"),
                                   QJsonObject()}}}}}});
}

class StdioTest : public ::testing::Test {
public:
    void SetUp() override {
        ASSERT_TRUE(output.open(QIODevice::ReadWrite));
        Limits limits;
        limits.stdioLinesPerWakeup = 16;
        ServerOptions options;
        options.limits = limits;
        options.version = QStringLiteral("test");
        server = std::make_unique<McpServer>(test::makeFakeRegistry(), consent, activity,
                                             std::move(options));
        transport = std::make_unique<StdioTransport>(*server, input, output);
    }

    [[nodiscard]] QByteArrayList outputLines() const {
        QByteArrayList lines = output.data().split('\n');
        EXPECT_TRUE(lines.back().isEmpty()); // every message ends with a newline
        lines.removeLast();
        return lines;
    }

    test::FakeConsentBroker consent;
    test::RecordingActivitySink activity;
    QBuffer output;
    TestLineSource input;
    std::unique_ptr<McpServer> server;
    std::unique_ptr<StdioTransport> transport;
};

TEST_F(StdioTest, OneJsonMessagePerLine) {
    input.push(echoLine(1));
    input.push("   ");
    input.push(R"({"jsonrpc":"2.0","method":"notifications/x"})");
    transport->start();
    input.push(echoLine(2));
    ASSERT_TRUE(test::runUntil([&] { return outputLines().size() == 2; }));
    const QByteArrayList lines = outputLines();
    for (int i = 0; i < 2; ++i) {
        const QJsonObject message = QJsonDocument::fromJson(lines.at(i)).object();
        EXPECT_EQ(message.value(QStringLiteral("id")).toInt(), i + 1);
        EXPECT_FALSE(lines.at(i).contains('\n'));
    }
}

TEST_F(StdioTest, OverlongLineIsAParseError) {
    transport->start();
    input.push({}, true);
    ASSERT_TRUE(test::runUntil([&] { return outputLines().size() == 1; }));
    const QJsonObject message = QJsonDocument::fromJson(outputLines().front()).object();
    EXPECT_EQ(
        message.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(),
        -32700);
    EXPECT_FALSE(message.contains(QStringLiteral("id")));
}

TEST_F(StdioTest, EndOfInputAfterTheLastLine) {
    int closed = 0;
    QObject::connect(transport.get(), &StdioTransport::inputClosed, [&closed] { ++closed; });
    for (int i = 0; i < 40; ++i) { // more than one wake-up budget
        input.push(echoLine(i));
    }
    input.end();
    transport->start();
    ASSERT_TRUE(test::runUntil([&] { return closed == 1; }));
    EXPECT_EQ(outputLines().size(), 40);
    test::runFor(10ms);
    EXPECT_EQ(closed, 1);
}

TEST_F(StdioTest, ReaderThreadWakesTheOwnerThread) {
    transport->start();
    constexpr int count = 300;
    std::jthread reader([this] {
        for (int i = 0; i < count; ++i) {
            input.push(echoLine(i));
        }
        input.end();
    });
    bool closed = false;
    QObject::connect(transport.get(), &StdioTransport::inputClosed, [&closed] { closed = true; });
    ASSERT_TRUE(test::runUntil([&] { return closed; }, 10s));
    reader.join();
    EXPECT_EQ(outputLines().size(), count);
}

TEST_F(StdioTest, OutputFailureClosesTheChannel) {
    int failed = 0;
    QObject::connect(transport.get(), &StdioTransport::outputFailed, [&failed] { ++failed; });
    transport->start();
    // A modern call that is still running when the output breaks is cancelled.
    input.push(serialize(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("tools/call")},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("slow")},
                     {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("step_ms"), 5000}}},
                     {QStringLiteral("_meta"),
                      QJsonObject{{QStringLiteral("io.modelcontextprotocol/protocolVersion"),
                                   QStringLiteral("2026-07-28")},
                                  {QStringLiteral("io.modelcontextprotocol/clientCapabilities"),
                                   QJsonObject()}}}}}}));
    ASSERT_TRUE(test::runUntil([&] { return server->activeCalls() == 1; }));
    output.close(); // writes fail from now on
    input.push(echoLine(2));
    ASSERT_TRUE(test::runUntil([&] { return failed == 1; }));
    EXPECT_EQ(server->activeCalls(), 0U);
    input.push(echoLine(3));
    test::runFor(10ms);
    EXPECT_EQ(failed, 1);
}

TEST_F(StdioTest, DestroyingTheTransportRemovesTheWakeup) {
    transport->start();
    transport.reset();
    input.push(echoLine(1)); // must not post to the destroyed transport
    test::runFor(10ms);
    EXPECT_TRUE(output.data().isEmpty());
}

} // namespace
} // namespace hio::mcp
