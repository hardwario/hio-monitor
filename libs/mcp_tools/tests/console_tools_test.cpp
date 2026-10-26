// Console tools against in-process FakeConsoleSessions (plan/features/0013 "Tests: Tools"):
// send_command (prompt, silence, timeout, echo filtering, interleaved, caps), read_terminal,
// read_log (cursor across eviction, pattern, bounds), wait_for_state (passive, polling, timeout,
// invalid regex), wait_for_connection, device resolution with 0/1/2 sessions, the device closed
// mid-call.

#include "hio/core/error.h"
#include "hio/device/console_line.h"
#include "hio/device/session_events.h"
#include "hio/mcp/limits.h"
#include "tools_test_support.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringLiteral>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

namespace hio::mcp::tools::test {
namespace {

using namespace std::chrono_literals;
using hio::device::Channel;
using hio::device::ConnectionState;
using hio::device::Direction;

std::vector<std::string> outputTexts(const PendingCall& call) {
    std::vector<std::string> texts;
    for (const auto& item : call.structured().value(QStringLiteral("output")).toArray()) {
        texts.push_back(item.toObject().value(QStringLiteral("text")).toString().toStdString());
    }
    return texts;
}

class ConsoleTools : public ::testing::Test {
protected:
    static hio::mcp::Limits limits() {
        hio::mcp::Limits shrunk;
        shrunk.tools.sendCommandSilence = 100ms;
        return shrunk;
    }
    [[nodiscard]] FakeProvider& provider() { return m_provider; }

private:
    FakeProvider m_provider;
};

TEST_F(ConsoleTools, RegistersEveryPhaseCTool) {
    Harness harness(provider());
    ASSERT_TRUE(harness.registered) << harness.registered.error().detail;
    std::vector<std::string> names;
    for (const auto& spec : harness.server->registry().tools()) {
        names.emplace_back(spec.name);
        EXPECT_FALSE(spec.outputSchema.isEmpty()) << spec.name;
        EXPECT_FALSE(spec.description.empty()) << spec.name;
    }
    const std::vector<std::string> expected{"list_devices",
                                            "connect",
                                            "disconnect",
                                            "status",
                                            "send_command",
                                            "read_terminal",
                                            "read_log",
                                            "wait_for_state",
                                            "wait_for_connection",
                                            "reconnect",
                                            "stop",
                                            "start",
                                            "release_probe",
                                            "reacquire_probe",
                                            "target_status",
                                            "reset",
                                            "halt",
                                            "go",
                                            "read_memory",
                                            "read_registers",
                                            "memory_zones",
                                            "read_pib",
                                            "read_uicr",
                                            "write_memory",
                                            "write_flash",
                                            "flash",
                                            "flash_lte",
                                            "erase",
                                            "recover"};
    EXPECT_EQ(names, expected);
}

TEST_F(ConsoleTools, DeviceArgumentWithZeroOneAndTwoSessions) {
    Harness harness(provider());
    auto none = harness.call(QStringLiteral("read_terminal"));
    EXPECT_EQ(none.errorCode(), "mcp.device_not_open");

    provider().add("jlink:1").shell("hello");
    auto one = harness.call(QStringLiteral("read_terminal"));
    ASSERT_FALSE(one.isError()) << one.errorCode();
    EXPECT_EQ(one.structured().value(QStringLiteral("lines")).toArray().size(), 1);

    provider().add("jlink:2");
    auto two = harness.call(QStringLiteral("read_terminal"));
    EXPECT_EQ(two.errorCode(), "mcp.device_ambiguous");
    EXPECT_EQ(two.errorDetail(), "jlink:1, jlink:2");

    auto named = harness.call(QStringLiteral("read_terminal"), args({{"device", "jlink:2"}}));
    EXPECT_FALSE(named.isError());
    auto unknown = harness.call(QStringLiteral("read_terminal"), args({{"device", "jlink:9"}}));
    EXPECT_EQ(unknown.errorCode(), "mcp.device_not_open");
    EXPECT_EQ(unknown.errorDetail(), "jlink:9");
}

TEST_F(ConsoleTools, SendCommandEndsBySilenceAndFiltersEchoes) {
    Harness harness(provider(), limits());
    auto& session = provider().add("jlink:1");
    session.shell("late output of an earlier command");
    auto call = harness.start(QStringLiteral("send_command"), args({{"command", "version"}}));
    ASSERT_EQ(session.sent(), std::vector<std::string>{"version"});
    session.shell("version"); // the shell's own echo of the typed line
    session.shell("Firmware version: v3");
    session.logLine("<inf> app: unrelated log");
    ASSERT_TRUE(runUntil([&] { return call.done(); }));
    ASSERT_FALSE(call.isError()) << call.errorCode();
    const QJsonObject result = call.structured();
    EXPECT_EQ(outputTexts(call), std::vector<std::string>{"Firmware version: v3"});
    EXPECT_EQ(result.value(QStringLiteral("end")).toString(), QStringLiteral("silence"));
    EXPECT_FALSE(result.value(QStringLiteral("interleaved")).toBool());
    EXPECT_FALSE(result.value(QStringLiteral("truncated")).toBool());
    EXPECT_LT(result.value(QStringLiteral("log_cursor")).toInteger(),
              result.value(QStringLiteral("log_cursor_end")).toInteger());
    // ADR-0105: a marker before the command and the "AI is running a command" hint.
    ASSERT_EQ(provider().record().markers.size(), 1U);
    EXPECT_TRUE(provider().record().markers[0].second.contains(QStringLiteral("AI (tester)")));
    ASSERT_EQ(provider().record().hints.size(), 2U);
    EXPECT_TRUE(provider().record().hints[0].second);
    EXPECT_FALSE(provider().record().hints[1].second);
    // The logs of the command: read_log after log_cursor.
    auto logs = harness.call(QStringLiteral("read_log"),
                             args({{"after_cursor", result.value(QStringLiteral("log_cursor"))}}));
    ASSERT_EQ(logs.structured().value(QStringLiteral("lines")).toArray().size(), 1);
}

TEST_F(ConsoleTools, SendCommandEndsAtThePrompt) {
    Harness harness(provider(), limits());
    auto& session = provider().add("jlink:1", promptProfile());
    auto call = harness.start(QStringLiteral("send_command"), args({{"command", "status"}}));
    session.shell("line 1");
    runFor(300ms); // no silence rule while a prompt is expected
    EXPECT_FALSE(call.done());
    session.shell("line 2");
    session.prompt();
    ASSERT_TRUE(runUntil([&] { return call.done(); }));
    EXPECT_EQ(call.structured().value(QStringLiteral("end")).toString(), QStringLiteral("prompt"));
    EXPECT_EQ(outputTexts(call), (std::vector<std::string>{"line 1", "line 2"}));
}

TEST_F(ConsoleTools, SendCommandTimeoutIsNotAnError) {
    Harness harness(provider(), limits());
    provider().add("jlink:1");
    auto call = harness.call(QStringLiteral("send_command"),
                             args({{"command", "silent"}, {"timeout", 0.2}}));
    ASSERT_FALSE(call.isError());
    EXPECT_EQ(call.structured().value(QStringLiteral("end")).toString(), QStringLiteral("timeout"));
    EXPECT_TRUE(call.structured().value(QStringLiteral("output")).toArray().isEmpty());
}

TEST_F(ConsoleTools, SendCommandReportsInterleavedCommands) {
    Harness harness(provider(), limits());
    auto& session = provider().add("jlink:1");
    auto call = harness.start(QStringLiteral("send_command"), args({{"command", "a"}}));
    session.shell("out a");
    session.emitLine(Channel::Shell, Direction::In, "b"); // the user typed another command
    session.shell("out b");
    ASSERT_TRUE(runUntil([&] { return call.done(); }));
    const QJsonObject result = call.structured();
    EXPECT_TRUE(result.value(QStringLiteral("interleaved")).toBool());
    const QJsonArray output = result.value(QStringLiteral("output")).toArray();
    ASSERT_EQ(output.size(), 3);
    EXPECT_EQ(output[1].toObject().value(QStringLiteral("dir")).toString(), QStringLiteral("in"));
    EXPECT_EQ(output[1].toObject().value(QStringLiteral("text")).toString(), QStringLiteral("b"));
}

TEST_F(ConsoleTools, SendCommandOutputIsCapped) {
    hio::mcp::Limits shrunk = limits();
    shrunk.tools.sendCommandMaxLines = 3;
    shrunk.tools.sendCommandMaxBytes = 1024;
    Harness harness(provider(), shrunk);
    auto& session = provider().add("jlink:1");
    auto call = harness.start(QStringLiteral("send_command"), args({{"command", "many"}}));
    for (int i = 0; i < 10; ++i) {
        session.shell("line " + std::to_string(i));
    }
    ASSERT_TRUE(runUntil([&] { return call.done(); }));
    EXPECT_EQ(outputTexts(call).size(), 3U);
    EXPECT_TRUE(call.structured().value(QStringLiteral("truncated")).toBool());

    shrunk.tools.sendCommandMaxLines = 100;
    shrunk.tools.sendCommandMaxBytes = 10;
    Harness bytes(provider(), shrunk);
    auto byBytes = bytes.start(QStringLiteral("send_command"), args({{"command", "long"}}));
    session.shell("12345678");
    session.shell("12345678");
    ASSERT_TRUE(runUntil([&] { return byBytes.done(); }));
    EXPECT_EQ(outputTexts(byBytes).size(), 1U);
    EXPECT_TRUE(byBytes.structured().value(QStringLiteral("truncated")).toBool());
}

TEST_F(ConsoleTools, SendCommandValidation) {
    Harness harness(provider());
    provider().add("jlink:1");
    auto twoLines = harness.call(QStringLiteral("send_command"), args({{"command", "a\nb"}}));
    EXPECT_EQ(twoLines.rpcError(), -32602);
    EXPECT_EQ(twoLines.rpcPointer(), QStringLiteral("/arguments/command"));
    auto tooLong =
        harness.call(QStringLiteral("send_command"), args({{"command", "x"}, {"timeout", 500}}));
    EXPECT_EQ(tooLong.rpcError(), -32602);
    auto missing = harness.call(QStringLiteral("send_command"));
    EXPECT_EQ(missing.rpcError(), -32602);
}

TEST_F(ConsoleTools, SendCommandDeviceClosedMidCall) {
    Harness harness(provider(), limits());
    provider().add("jlink:1");
    auto call = harness.start(QStringLiteral("send_command"), args({{"command", "x"}}));
    provider().remove("jlink:1"); // the GUI tab closes: the QPointers become null
    ASSERT_TRUE(runUntil([&] { return call.done(); }));
    EXPECT_EQ(call.errorCode(), "mcp.device_not_open");
}

TEST_F(ConsoleTools, ReadTerminalNewestAndAfterCursor) {
    Harness harness(provider());
    auto& session = provider().add("jlink:1");
    for (int i = 0; i < 5; ++i) {
        session.shell("s" + std::to_string(i));
    }
    session.logLine("not a terminal line");
    auto newest = harness.call(QStringLiteral("read_terminal"), args({{"lines", 2}}));
    const QJsonArray lines = newest.structured().value(QStringLiteral("lines")).toArray();
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0].toObject().value(QStringLiteral("text")).toString(), QStringLiteral("s3"));
    EXPECT_EQ(lines[1].toObject().value(QStringLiteral("dir")).toString(), QStringLiteral("out"));
    EXPECT_FALSE(lines[1].toObject().value(QStringLiteral("time")).toString().isEmpty());
    EXPECT_EQ(newest.structured().value(QStringLiteral("cursor_end")).toInteger(), 6);

    auto page =
        harness.call(QStringLiteral("read_terminal"), args({{"after_cursor", 1}, {"lines", 2}}));
    const QJsonArray paged = page.structured().value(QStringLiteral("lines")).toArray();
    ASSERT_EQ(paged.size(), 2);
    EXPECT_EQ(paged[0].toObject().value(QStringLiteral("seq")).toInteger(), 2);
    EXPECT_EQ(page.structured().value(QStringLiteral("cursor_end")).toInteger(), 3);
    auto rest =
        harness.call(QStringLiteral("read_terminal"), args({{"after_cursor", 3}, {"lines", 100}}));
    EXPECT_EQ(rest.structured().value(QStringLiteral("lines")).toArray().size(), 2);
    EXPECT_EQ(rest.structured().value(QStringLiteral("cursor_end")).toInteger(), 6);
}

// ADR-0155: a cursor that points to evicted lines reports how many were lost.
TEST_F(ConsoleTools, ReadLogCursorAcrossEviction) {
    Harness harness(provider());
    auto& session = provider().add("jlink:1", chesterProfile(), 10);
    for (int i = 0; i < 5; ++i) {
        session.logLine("old " + std::to_string(i));
    }
    const qint64 cursor = 5;
    for (int i = 0; i < 30; ++i) {
        session.logLine("new " + std::to_string(i));
    }
    auto read = harness.call(QStringLiteral("read_log"), args({{"after_cursor", cursor}}));
    const QJsonObject result = read.structured();
    const QJsonArray lines = result.value(QStringLiteral("lines")).toArray();
    ASSERT_EQ(lines.size(), 10);
    EXPECT_EQ(lines[0].toObject().value(QStringLiteral("text")).toString(),
              QStringLiteral("new 20"));
    EXPECT_EQ(lines[0].toObject().value(QStringLiteral("level")).toString(), QStringLiteral("inf"));
    EXPECT_EQ(result.value(QStringLiteral("dropped_before")).toInteger(), 20);
    EXPECT_EQ(result.value(QStringLiteral("cursor_end")).toInteger(), 35);
    EXPECT_FALSE(result.value(QStringLiteral("truncated")).toBool());
    auto current = harness.call(QStringLiteral("read_log"), args({{"after_cursor", 30}}));
    EXPECT_EQ(current.structured().value(QStringLiteral("dropped_before")).toInteger(), 0);
    EXPECT_EQ(current.structured().value(QStringLiteral("lines")).toArray().size(), 5);
}

TEST_F(ConsoleTools, ReadLogPatternAndBounds) {
    hio::mcp::Limits shrunk;
    shrunk.tools.readMaxLines = 3;
    Harness harness(provider(), shrunk);
    auto& session = provider().add("jlink:1");
    for (int i = 0; i < 6; ++i) {
        session.logLine((i % 2 == 0 ? "<inf> lte: attached " : "<dbg> other ") + std::to_string(i));
    }
    auto filtered = harness.call(QStringLiteral("read_log"), args({{"pattern", "ATTACHED"}}));
    EXPECT_EQ(filtered.structured().value(QStringLiteral("lines")).toArray().size(), 3);
    EXPECT_FALSE(filtered.structured().value(QStringLiteral("truncated")).toBool());
    auto all = harness.call(QStringLiteral("read_log"));
    EXPECT_EQ(all.structured().value(QStringLiteral("lines")).toArray().size(), 3);
    EXPECT_TRUE(all.structured().value(QStringLiteral("truncated")).toBool());
    auto two = harness.call(QStringLiteral("read_log"), args({{"lines", 2}}));
    EXPECT_EQ(two.structured().value(QStringLiteral("lines")).toArray().size(), 2);
    EXPECT_FALSE(two.structured().value(QStringLiteral("truncated")).toBool());
    auto forward = harness.call(QStringLiteral("read_log"), args({{"after_cursor", 0}}));
    EXPECT_TRUE(forward.structured().value(QStringLiteral("truncated")).toBool());
    EXPECT_EQ(forward.structured().value(QStringLiteral("cursor_end")).toInteger(), 3);

    auto invalid = harness.call(QStringLiteral("read_log"), args({{"pattern", "(unclosed"}}));
    EXPECT_EQ(invalid.rpcError(), -32602);
    EXPECT_EQ(invalid.rpcPointer(), QStringLiteral("/arguments/pattern"));
}

// Review R10: a pattern scans at most readScanMaxLines lines per call; with after_cursor the call
// ends early with a continuation cursor, without it only the newest lines are searched.
TEST_F(ConsoleTools, ReadLogPatternScanIsCapped) {
    hio::mcp::Limits shrunk;
    shrunk.tools.readScanMaxLines = 100;
    Harness harness(provider(), shrunk);
    auto& session = provider().add("jlink:1");
    for (int i = 0; i < 250; ++i) {
        session.logLine((i == 20 || i == 230 ? "needle " : "hay ") + std::to_string(i));
    }
    QJsonObject result =
        harness.call(QStringLiteral("read_log"), args({{"pattern", "needle"}, {"after_cursor", 0}}))
            .structured();
    ASSERT_EQ(result.value(QStringLiteral("lines")).toArray().size(), 1);
    EXPECT_EQ(result.value(QStringLiteral("lines"))
                  .toArray()
                  .at(0)
                  .toObject()
                  .value(QStringLiteral("text")),
              QStringLiteral("needle 20"));
    EXPECT_TRUE(result.value(QStringLiteral("truncated")).toBool());
    EXPECT_EQ(result.value(QStringLiteral("cursor_end")).toInteger(), 100); // last scanned line
    // Continue: lines 101-200 hold no match.
    result =
        harness
            .call(QStringLiteral("read_log"), args({{"pattern", "needle"}, {"after_cursor", 100}}))
            .structured();
    EXPECT_TRUE(result.value(QStringLiteral("lines")).toArray().isEmpty());
    EXPECT_TRUE(result.value(QStringLiteral("truncated")).toBool());
    EXPECT_EQ(result.value(QStringLiteral("cursor_end")).toInteger(), 200);
    result =
        harness
            .call(QStringLiteral("read_log"), args({{"pattern", "needle"}, {"after_cursor", 200}}))
            .structured();
    ASSERT_EQ(result.value(QStringLiteral("lines")).toArray().size(), 1);
    EXPECT_FALSE(result.value(QStringLiteral("truncated")).toBool());
    EXPECT_EQ(result.value(QStringLiteral("cursor_end")).toInteger(), 250);

    // Newest mode: only the newest 100 lines are searched (needle 20 is older).
    result = harness.call(QStringLiteral("read_log"), args({{"pattern", "needle"}})).structured();
    ASSERT_EQ(result.value(QStringLiteral("lines")).toArray().size(), 1);
    EXPECT_EQ(result.value(QStringLiteral("lines"))
                  .toArray()
                  .at(0)
                  .toObject()
                  .value(QStringLiteral("text")),
              QStringLiteral("needle 230"));
    EXPECT_TRUE(result.value(QStringLiteral("truncated")).toBool());
    // Without a pattern the scan cap does not apply.
    result = harness.call(QStringLiteral("read_log"), args({{"after_cursor", 0}})).structured();
    EXPECT_EQ(result.value(QStringLiteral("lines")).toArray().size(), 250);
    EXPECT_FALSE(result.value(QStringLiteral("truncated")).toBool());
}

// Review R10: only the first regexMaxLineBytes bytes of a line are matched.
TEST_F(ConsoleTools, PatternMatchesTheStartOfLongLinesOnly) {
    hio::mcp::Limits shrunk;
    shrunk.tools.regexMaxLineBytes = 16;
    Harness harness(provider(), shrunk);
    auto& session = provider().add("jlink:1");
    session.logLine(std::string(40, 'x') + " TARGET late");
    session.logLine("TARGET early " + std::string(40, 'x'));
    const QJsonArray lines = harness.call(QStringLiteral("read_log"), args({{"pattern", "target"}}))
                                 .structured()
                                 .value(QStringLiteral("lines"))
                                 .toArray();
    ASSERT_EQ(lines.size(), 1);
    EXPECT_TRUE(lines.at(0)
                    .toObject()
                    .value(QStringLiteral("text"))
                    .toString()
                    .startsWith(QStringLiteral("TARGET early")));

    auto late = harness.start(QStringLiteral("wait_for_state"),
                              args({{"pattern", "found"}, {"timeout", 5}}));
    session.logLine(std::string(40, 'y') + " found");
    runFor(100ms);
    EXPECT_FALSE(late.done());
    session.logLine("found it");
    ASSERT_TRUE(runUntil([&] { return late.done(); }));
    EXPECT_EQ(late.structured().value(QStringLiteral("matched_line")).toString(),
              QStringLiteral("found it"));
}

// Review R10: at the timeout the unscanned backlog is scanned in scheduler turns (not drained in
// one go) and still counts; lines that arrived before the timeout are matched.
TEST_F(ConsoleTools, WaitForStateTimeoutScansTheBacklog) {
    Harness harness(provider());
    auto& session = provider().add("jlink:1");
    auto matched = harness.start(QStringLiteral("wait_for_state"),
                                 args({{"pattern", "ready"}, {"timeout", 1}}));
    // 20 000 lines reach the log without linesReceived (no scan was requested for them).
    for (int i = 0; i < 20000; ++i) {
        hio::device::ConsoleLine line;
        line.channel = Channel::Log;
        line.dir = Direction::Out;
        line.time = std::chrono::system_clock::now();
        line.text = i == 19999 ? "system ready" : "noise " + std::to_string(i);
        (void)session.mutableLog().append(line);
    }
    ASSERT_TRUE(runUntil([&] { return matched.done(); }, 10s));
    EXPECT_EQ(matched.structured().value(QStringLiteral("status")).toString(),
              QStringLiteral("ok"));
    EXPECT_EQ(matched.structured().value(QStringLiteral("matched_line")).toString(),
              QStringLiteral("system ready"));

    auto timedOut = harness.start(QStringLiteral("wait_for_state"),
                                  args({{"pattern", "never"}, {"timeout", 1}}));
    for (int i = 0; i < 5000; ++i) {
        hio::device::ConsoleLine line;
        line.channel = Channel::Log;
        line.dir = Direction::Out;
        line.time = std::chrono::system_clock::now();
        line.text = "line " + std::to_string(i);
        (void)session.mutableLog().append(line);
    }
    ASSERT_TRUE(runUntil([&] { return timedOut.done(); }, 10s));
    const QJsonObject result = timedOut.structured();
    EXPECT_EQ(result.value(QStringLiteral("status")).toString(), QStringLiteral("timeout"));
    const QJsonArray last = result.value(QStringLiteral("last_lines")).toArray();
    ASSERT_EQ(last.size(), 10);
    EXPECT_EQ(last.at(9).toString(), QStringLiteral("line 4999"));
}

TEST_F(ConsoleTools, WaitForStatePassive) {
    Harness harness(provider());
    auto& session = provider().add("jlink:1");
    session.logLine("cloud state: initialized: yes"); // history is ignored
    auto call = harness.start(QStringLiteral("wait_for_state"),
                              args({{"pattern", "initialized: yes"}, {"timeout", 5}}), true);
    runFor(50ms);
    EXPECT_FALSE(call.done());
    session.logLine("cloud state: initialized: no");
    session.shell("initialized: yes"); // terminal output is not watched with source=log
    runFor(50ms);
    EXPECT_FALSE(call.done());
    session.logLine("cloud state: Initialized: YES");
    ASSERT_TRUE(runUntil([&] { return call.done(); }));
    const QJsonObject result = call.structured();
    EXPECT_EQ(result.value(QStringLiteral("status")).toString(), QStringLiteral("ok"));
    EXPECT_TRUE(result.value(QStringLiteral("matched")).toBool());
    EXPECT_EQ(result.value(QStringLiteral("matched_line")).toString(),
              QStringLiteral("cloud state: Initialized: YES"));
    EXPECT_TRUE(provider().record().markers.empty()); // passive: no marker
}

TEST_F(ConsoleTools, WaitForStatePollingSendsTheCommand) {
    Harness harness(provider());
    auto& session = provider().add("jlink:1");
    auto call = harness.start(QStringLiteral("wait_for_state"), args({{"pattern", "attached: yes"},
                                                                      {"command", "lte state"},
                                                                      {"poll_interval", 0.5},
                                                                      {"source", "terminal"}}));
    ASSERT_EQ(session.sent().size(), 1U); // at once
    session.shell("attached: no");
    ASSERT_TRUE(runUntil([&] { return session.sent().size() >= 2; }, 3s));
    EXPECT_FALSE(call.done());
    session.shell("attached: yes");
    ASSERT_TRUE(runUntil([&] { return call.done(); }));
    EXPECT_TRUE(call.structured().value(QStringLiteral("matched")).toBool());
    EXPECT_EQ(provider().record().markers.size(), 1U); // polling writes a marker
    ASSERT_FALSE(provider().record().hints.empty());
    EXPECT_FALSE(provider().record().hints.back().second);
}

TEST_F(ConsoleTools, WaitForStateTimeoutAndInvalidRegex) {
    Harness harness(provider());
    auto& session = provider().add("jlink:1");
    auto call = harness.start(QStringLiteral("wait_for_state"),
                              args({{"pattern", "never"}, {"timeout", 1}}));
    session.logLine("a");
    session.logLine("b");
    ASSERT_TRUE(runUntil([&] { return call.done(); }, 5s));
    const QJsonObject result = call.structured();
    EXPECT_FALSE(call.isError());
    EXPECT_EQ(result.value(QStringLiteral("status")).toString(), QStringLiteral("timeout"));
    EXPECT_FALSE(result.value(QStringLiteral("matched")).toBool());
    EXPECT_EQ(result.value(QStringLiteral("last_lines")).toArray(),
              (QJsonArray{QStringLiteral("a"), QStringLiteral("b")}));

    auto invalid = harness.call(QStringLiteral("wait_for_state"), args({{"pattern", "[a-"}}));
    EXPECT_EQ(invalid.rpcError(), -32602);
    EXPECT_EQ(invalid.rpcPointer(), QStringLiteral("/arguments/pattern"));
    auto tooLong = harness.call(QStringLiteral("wait_for_state"),
                                args({{"pattern", QString(513, QLatin1Char('a'))}}));
    EXPECT_EQ(tooLong.rpcError(), -32602);
}

TEST_F(ConsoleTools, WaitForConnection) {
    Harness harness(provider());
    auto& session = provider().add("jlink:1");
    auto connected = harness.call(QStringLiteral("wait_for_connection"));
    EXPECT_TRUE(connected.structured().value(QStringLiteral("connected")).toBool());

    session.setConnection(ConnectionState::Disconnected, makeError(ErrorCode::TargetRttNotFound));
    auto call = harness.start(QStringLiteral("wait_for_connection"), args({{"timeout", 5}}));
    runFor(50ms);
    EXPECT_FALSE(call.done());
    session.setConnection(ConnectionState::Connected);
    ASSERT_TRUE(runUntil([&] { return call.done(); }));
    EXPECT_EQ(call.structured().value(QStringLiteral("status")).toString(), QStringLiteral("ok"));

    session.setConnection(ConnectionState::Disconnected, makeError(ErrorCode::TargetRttNotFound));
    auto timeout = harness.call(QStringLiteral("wait_for_connection"), args({{"timeout", 1}}));
    const QJsonObject result = timeout.structured();
    EXPECT_EQ(result.value(QStringLiteral("status")).toString(), QStringLiteral("timeout"));
    EXPECT_EQ(result.value(QStringLiteral("state")).toString(), QStringLiteral("disconnected"));
    EXPECT_EQ(
        result.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
        QStringLiteral("target.rtt_not_found"));
}

TEST_F(ConsoleTools, StatusOfAllAndOneDevice) {
    Harness harness(provider());
    provider().add("jlink:1").logLine("x");
    provider().add("jlink:2");
    auto all = harness.call(QStringLiteral("status"));
    const QJsonArray devices = all.structured().value(QStringLiteral("devices")).toArray();
    ASSERT_EQ(devices.size(), 2);
    const QJsonObject first = devices[0].toObject();
    EXPECT_EQ(first.value(QStringLiteral("log_total")).toInteger(), 1);
    EXPECT_EQ(first.value(QStringLiteral("buffer_size")).toInteger(), 100000);
    EXPECT_EQ(first.value(QStringLiteral("connection")).toObject().value(QStringLiteral("state")),
              QStringLiteral("connected"));
    EXPECT_TRUE(first.value(QStringLiteral("busy_operation")).isNull());
    auto one = harness.call(QStringLiteral("status"), args({{"device", "jlink:2"}}));
    EXPECT_EQ(one.structured().value(QStringLiteral("devices")).toArray().size(), 1);
}

// Control tools on a session without J-Link (BLE): mcp.capability_missing.
TEST_F(ConsoleTools, JLinkToolsNeedAJLinkSession) {
    Harness harness(provider());
    auto& session = provider().add("ble:1");
    for (const char* tool :
         {"stop", "start", "release_probe", "reacquire_probe", "halt", "go", "target_status",
          "read_registers", "memory_zones", "read_pib", "read_uicr", "erase", "recover"}) {
        auto call = harness.call(QString::fromLatin1(tool));
        EXPECT_EQ(call.errorCode(), "mcp.capability_missing") << tool;
    }
    auto reconnectAddress =
        harness.call(QStringLiteral("reconnect"), args({{"rtt_address", "0x20000000"}}));
    EXPECT_EQ(reconnectAddress.errorCode(), "mcp.capability_missing");
    auto reconnect = harness.call(QStringLiteral("reconnect"));
    EXPECT_FALSE(reconnect.isError());
    EXPECT_EQ(session.reconnects(), 1);
}

} // namespace
} // namespace hio::mcp::tools::test
