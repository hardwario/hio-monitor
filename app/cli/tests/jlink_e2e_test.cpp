// End-to-end tests of the M1 commands (plan/features/0002, phase F5): the real `hio` executable
// with the test-only fake J-Link library (tests/fake_jlink) and JSON scenarios.

#include "cli_process.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QtContainerFwd>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#ifndef _WIN32
#include <csignal>
#endif

namespace {

using hio::cli::test::CliProcess;
using hio::cli::test::RunResult;

constexpr const char* twoProbes = R"({
  "probes": [
    {"serialNumber": 801000001, "product": "Test Probe",
     "rtt": {"output": [{"atMs": 0, "buffer": 1, "text": "[00:00:00.001,000] <inf> app: Boot\n"}],
             "responses": {"version": "Firmware version: v3.1.0\n",
                           "status": "line 1\nline 2\n"}}},
    {"serialNumber": 801000002, "product": "Test Probe 2"}
  ]})";

std::vector<QJsonObject> envelopes(const std::string& out) {
    std::vector<QJsonObject> result;
    for (const QByteArray& line : QByteArray::fromStdString(out).split('\n')) {
        if (!line.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(line);
            EXPECT_TRUE(doc.isObject()) << line.toStdString();
            result.push_back(doc.object());
        }
    }
    return result;
}

QJsonObject data(const QJsonObject& envelope) {
    return envelope.value(QStringLiteral("data")).toObject();
}

QString errorCode(const RunResult& result) {
    const auto all = envelopes(result.out);
    return all.empty() ? QString()
                       : all.back()
                             .value(QStringLiteral("error"))
                             .toObject()
                             .value(QStringLiteral("code"))
                             .toString();
}

// The number of {"type":"connection","state":"stopped"} events in an NDJSON stream (review R28).
int stoppedEvents(const std::string& out) {
    int count = 0;
    for (const QJsonObject& envelope : envelopes(out)) {
        const QJsonObject event = data(envelope);
        count += event.value(QStringLiteral("type")) == QStringLiteral("connection") &&
                         event.value(QStringLiteral("state")) == QStringLiteral("stopped")
                     ? 1
                     : 0;
    }
    return count;
}

class FakeJLinkCli : public CliProcess {
protected:
    void SetUp() override { useScenario(twoProbes); }

    void useScenario(const char* json) const {
        QFile file(home() + QStringLiteral("/scenario.json"));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(json);
    }

    [[nodiscard]] QProcessEnvironment fakeEnvironment() const {
        QProcessEnvironment env = environment();
        env.insert(QStringLiteral("HIO_FAKE_JLINK_SCENARIO"),
                   home() + QStringLiteral("/scenario.json"));
        env.insert(QStringLiteral("HIO_JLINK_LIBRARY"), QStringLiteral(HIO_FAKE_JLINK_LIBRARY));
        return env;
    }

    [[nodiscard]] RunResult hio(const QStringList& args, const QByteArray& input = {}) const {
        return run(args, fakeEnvironment(), input);
    }
};

TEST_F(FakeJLinkCli, JLinkListJson) {
    const RunResult result =
        hio({QStringLiteral("--json"), QStringLiteral("jlink"), QStringLiteral("list")});
    ASSERT_TRUE(result.normalExit);
    EXPECT_EQ(result.exitCode, 0) << result.err;
    const auto all = envelopes(result.out);
    ASSERT_EQ(all.size(), 1U);
    const QJsonObject list = data(all[0]);
    EXPECT_EQ(list.value(QStringLiteral("library")).toObject().value(QStringLiteral("version")),
              QStringLiteral("8.10"));
    const QJsonArray probes = list.value(QStringLiteral("probes")).toArray();
    ASSERT_EQ(probes.size(), 2);
    const QJsonObject first = probes.at(0).toObject();
    EXPECT_EQ(first.value(QStringLiteral("serialNumber")).toInteger(), 801000001);
    EXPECT_EQ(first.value(QStringLiteral("product")).toString(), QStringLiteral("Test Probe"));
    EXPECT_EQ(first.value(QStringLiteral("connection")).toString(), QStringLiteral("usb"));
    EXPECT_TRUE(first.value(QStringLiteral("inUseBy")).isNull());
    // `list` never opens a probe: unknown firmware is null (decided 2026-10-09).
    EXPECT_TRUE(first.value(QStringLiteral("firmware")).isNull());

    const RunResult human = hio({QStringLiteral("jlink"), QStringLiteral("list")});
    EXPECT_NE(human.out.find("801000001      Test Probe    usb         \u2014         \u2014"),
              std::string::npos)
        << human.out;
}

// Review R12: `jlink list` shows USB and network J-Links with the connection column.
TEST_F(FakeJLinkCli, JLinkListShowsNetworkProbes) {
    useScenario(R"({"probes": [{"serialNumber": 70, "product": "Test Probe IP",
                                "connection": "ip"}, {"serialNumber": 71}]})");
    const RunResult result =
        hio({QStringLiteral("--json"), QStringLiteral("jlink"), QStringLiteral("list")});
    EXPECT_EQ(result.exitCode, 0);
    const QJsonArray probes =
        data(envelopes(result.out).at(0)).value(QStringLiteral("probes")).toArray();
    ASSERT_EQ(probes.size(), 2);
    EXPECT_EQ(probes.at(0).toObject().value(QStringLiteral("connection")).toString(),
              QStringLiteral("ip"));
    EXPECT_EQ(probes.at(1).toObject().value(QStringLiteral("connection")).toString(),
              QStringLiteral("usb"));
}

TEST_F(FakeJLinkCli, JLinkListWithoutProbesIsEmpty) {
    useScenario(R"({"probes": []})");
    const RunResult result =
        hio({QStringLiteral("--json"), QStringLiteral("jlink"), QStringLiteral("list")});
    EXPECT_EQ(result.exitCode, 0);
    const auto all = envelopes(result.out);
    ASSERT_EQ(all.size(), 1U);
    EXPECT_TRUE(data(all[0]).value(QStringLiteral("probes")).toArray().isEmpty());

    const RunResult human = hio({QStringLiteral("jlink"), QStringLiteral("list")});
    EXPECT_EQ(human.exitCode, 0);
    EXPECT_NE(human.out.find("No J-Link probe found."), std::string::npos) << human.out;
}

TEST_F(FakeJLinkCli, JLinkInfo) {
    const RunResult result =
        hio({QStringLiteral("--json"), QStringLiteral("jlink"), QStringLiteral("info"),
             QStringLiteral("-n"), QStringLiteral("801000002")});
    EXPECT_EQ(result.exitCode, 0) << result.err;
    const QJsonObject info = data(envelopes(result.out).at(0));
    EXPECT_EQ(info.value(QStringLiteral("serialNumber")).toInteger(), 801000002);
    EXPECT_EQ(info.value(QStringLiteral("firmwareStatus")).toString(), QStringLiteral("current"));
    EXPECT_EQ(info.value(QStringLiteral("targetMillivolts")).toInt(), 3300);
}

TEST_F(FakeJLinkCli, UpdateFirmwareNeedsConfirmationWhenNotInteractive) {
    useScenario(R"({"probes": [{"serialNumber": 7,
        "firmware": "J-Link V11 compiled Sep 21 2023 10:38:43",
        "libraryFirmware": "J-Link V11 compiled Oct  1 2024 09:00:00"}]})");
    const RunResult refused = hio({QStringLiteral("jlink"), QStringLiteral("update-firmware")});
    EXPECT_EQ(refused.exitCode, 2);
    EXPECT_NE(refused.err.find("This operation needs confirmation."), std::string::npos)
        << refused.err;

    const RunResult updated = hio({QStringLiteral("--json"), QStringLiteral("jlink"),
                                   QStringLiteral("update-firmware"), QStringLiteral("--yes")});
    EXPECT_EQ(updated.exitCode, 0) << updated.err;
    const QJsonObject result = data(envelopes(updated.out).at(0));
    EXPECT_EQ(result.value(QStringLiteral("before")).toString(), QStringLiteral("outdated"));
    EXPECT_TRUE(result.value(QStringLiteral("updated")).toBool());
}

TEST_F(FakeJLinkCli, CommandVersionJson) {
    const RunResult result =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("command"), QStringLiteral("version"), QStringLiteral("--console-file"),
             QString()});
    ASSERT_TRUE(result.normalExit);
    EXPECT_EQ(result.exitCode, 0) << result.err;
    const auto all = envelopes(result.out);
    ASSERT_EQ(all.size(), 1U);
    const QJsonObject response = data(all[0]);
    EXPECT_EQ(response.value(QStringLiteral("command")).toString(), QStringLiteral("version"));
    const QJsonArray output = response.value(QStringLiteral("output")).toArray();
    ASSERT_EQ(output.size(), 1);
    EXPECT_EQ(output.at(0).toString(), QStringLiteral("Firmware version: v3.1.0"));
    EXPECT_TRUE(response.value(QStringLiteral("log")).isArray());
    EXPECT_EQ(response.value(QStringLiteral("truncated")), QJsonValue(false)); // review R42
}

TEST_F(FakeJLinkCli, CommandHumanOutputAndSerialOnStderr) {
    const RunResult result =
        hio({QStringLiteral("chester"), QStringLiteral("app"), QStringLiteral("command"),
             QStringLiteral("status"), QStringLiteral("--timeout"), QStringLiteral("0.5")});
    EXPECT_EQ(result.exitCode, 0) << result.err;
    EXPECT_EQ(result.out, "line 1\nline 2\n");
    EXPECT_NE(result.err.find("Using J-Link 801000001"), std::string::npos) << result.err;
}

TEST_F(FakeJLinkCli, ConsoleJsonWithPipedStdin) {
    const RunResult result =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("console"), QStringLiteral("--console-file"), QString()},
            "version\n");
    ASSERT_TRUE(result.normalExit);
    EXPECT_EQ(result.exitCode, 0) << result.err;
    const auto all = envelopes(result.out);
    ASSERT_GE(all.size(), 4U) << result.out;
    EXPECT_EQ(data(all.front()).value(QStringLiteral("type")).toString(),
              QStringLiteral("connection"));
    EXPECT_EQ(data(all.front()).value(QStringLiteral("state")).toString(),
              QStringLiteral("connecting"));
    EXPECT_EQ(data(all.back()).value(QStringLiteral("state")).toString(),
              QStringLiteral("stopped"));
    EXPECT_EQ(stoppedEvents(result.out), 1) << result.out; // review R28
    bool echo = false;
    bool response = false;
    bool log = false;
    for (const QJsonObject& envelope : all) {
        EXPECT_TRUE(envelope.value(QStringLiteral("ok")).toBool());
        const QJsonObject event = data(envelope);
        if (event.value(QStringLiteral("type")).toString() != QStringLiteral("line")) {
            continue;
        }
        const QString text = event.value(QStringLiteral("text")).toString();
        echo = echo || (event.value(QStringLiteral("dir")) == QStringLiteral("in") &&
                        text == QStringLiteral("version"));
        response = response || text == QStringLiteral("Firmware version: v3.1.0");
        log = log || (event.value(QStringLiteral("channel")) == QStringLiteral("log") &&
                      event.value(QStringLiteral("level")) == QStringLiteral("inf"));
        EXPECT_TRUE(event.value(QStringLiteral("seq")).toInteger() > 0);
        EXPECT_TRUE(event.value(QStringLiteral("time")).toString().endsWith(QLatin1Char('Z')));
    }
    EXPECT_TRUE(echo) << result.out;
    EXPECT_TRUE(response) << result.out;
    EXPECT_TRUE(log) << result.out;
}

TEST_F(FakeJLinkCli, MissingLibraryIsDependencyError) {
    const RunResult result =
        hio({QStringLiteral("--jlink-library"), QStringLiteral("/nonexistent/libjlinkarm.so"),
             QStringLiteral("--json"), QStringLiteral("jlink"), QStringLiteral("list")});
    EXPECT_EQ(result.exitCode, 4);
    EXPECT_EQ(errorCode(result), QStringLiteral("dependency.jlink_not_found"));
}

TEST_F(FakeJLinkCli, MissingProbeIsProbeError) {
    useScenario(R"({"probes": []})");
    const RunResult result =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("command"), QStringLiteral("version")});
    EXPECT_EQ(result.exitCode, 5);
    EXPECT_EQ(errorCode(result), QStringLiteral("probe.not_found"));
    const RunResult bySerial =
        hio({QStringLiteral("chester"), QStringLiteral("app"), QStringLiteral("-n"),
             QStringLiteral("123"), QStringLiteral("command"), QStringLiteral("version")});
    EXPECT_EQ(bySerial.exitCode, 5);
    EXPECT_NE(bySerial.err.find("No J-Link probe with serial number 123 found."), std::string::npos)
        << bySerial.err;
}

// An unpowered board reads a low non-zero VTref; exactly 0 mV before a healthy reading is
// target.connect_failed (review R21).
TEST_F(FakeJLinkCli, NoPowerIsTargetError) {
    useScenario(R"({"probes": [{"serialNumber": 1, "vtref": [[0, 150]]}]})");
    const RunResult result =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("console"), QStringLiteral("--console-file"), QString()});
    EXPECT_EQ(result.exitCode, 6);
    EXPECT_EQ(errorCode(result), QStringLiteral("target.no_power"));
    // Exactly one terminal "stopped" event, then the error envelope (review R28).
    EXPECT_EQ(stoppedEvents(result.out), 1) << result.out;
    const auto all = envelopes(result.out);
    ASSERT_GE(all.size(), 2U) << result.out;
    EXPECT_FALSE(all.back().value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(data(all.at(all.size() - 2)).value(QStringLiteral("state")),
              QStringLiteral("stopped"));
}

// ADR-0090: --jlink-speed is `auto` or 5-50 000 kHz (review R49).
TEST_F(FakeJLinkCli, InvalidSpeedIsUsageError) {
    for (const QString& speed : {QStringLiteral("1"), QStringLiteral("fast")}) {
        const RunResult result =
            hio({QStringLiteral("chester"), QStringLiteral("app"), QStringLiteral("command"),
                 QStringLiteral("--jlink-speed"), speed, QStringLiteral("version")});
        EXPECT_EQ(result.exitCode, 2) << speed.toStdString();
    }
}

// ADR-0110: a fixed speed below 125 kHz is warned about on stderr before connecting.
TEST_F(FakeJLinkCli, SlowSpeedIsWarnedBeforeConnecting) {
    const std::string warning = "Warning: At this J-Link speed the console may not be found "
                                "within 60 s. Use auto or 1000 kHz or more.";
    const RunResult slow =
        hio({QStringLiteral("chester"), QStringLiteral("app"), QStringLiteral("command"),
             QStringLiteral("--jlink-speed"), QStringLiteral("100"), QStringLiteral("version"),
             QStringLiteral("--timeout"), QStringLiteral("0.5")});
    EXPECT_EQ(slow.exitCode, 0) << slow.err;
    EXPECT_NE(slow.err.find(warning), std::string::npos) << slow.err;
    EXPECT_LT(slow.err.find(warning), slow.err.find("Using J-Link")) << slow.err;
    const RunResult fast =
        hio({QStringLiteral("chester"), QStringLiteral("app"), QStringLiteral("command"),
             QStringLiteral("--jlink-speed"), QStringLiteral("125"), QStringLiteral("version"),
             QStringLiteral("--timeout"), QStringLiteral("0.5")});
    EXPECT_EQ(fast.exitCode, 0) << fast.err;
    EXPECT_EQ(fast.err.find("Warning:"), std::string::npos) << fast.err;
    // --json: stdout stays the single result envelope, stderr stays free of the human text.
    const RunResult json =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("command"), QStringLiteral("--jlink-speed"), QStringLiteral("100"),
             QStringLiteral("version"), QStringLiteral("--timeout"), QStringLiteral("0.5")});
    EXPECT_EQ(json.exitCode, 0) << json.err;
    EXPECT_EQ(envelopes(json.out).size(), 1U) << json.out;
    EXPECT_EQ(json.err.find(warning), std::string::npos) << json.err;
}

// Review fix 7: the collection window starts when the command was written, not when it was
// queued. The fake accepts the write 1.5 s after the first attempt (relative to the write, not to
// boot; review R30): a window of 1 s from queueing would end before the reply exists, the window
// from the write (the echo) has 1 s for a reply that is there at once.
TEST_F(FakeJLinkCli, CommandTimeoutCountsFromTheWrite) {
    useScenario(R"({"probes": [{"serialNumber": 5, "rtt": {
        "acceptWritesAfterMs": 1500,
        "responses": {"version": "Firmware version: v3.1.0\n"}}}]})");
    const RunResult result =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("command"), QStringLiteral("version"), QStringLiteral("--timeout"),
             QStringLiteral("1"), QStringLiteral("--console-file"), QString()});
    EXPECT_EQ(result.exitCode, 0) << result.err;
    const auto all = envelopes(result.out);
    ASSERT_FALSE(all.empty()) << result.out;
    const QJsonArray output = data(all.at(0)).value(QStringLiteral("output")).toArray();
    ASSERT_EQ(output.size(), 1) << result.out;
    EXPECT_EQ(output.at(0).toString(), QStringLiteral("Firmware version: v3.1.0"));
}

// Review R23 / R36: the echo is awaited for one RTT write timeout (2 s) per line + 500 ms. Each
// of the two lines is written only 1.5 s after its first attempt (fake acceptEachWriteAfterMs):
// 3 s in total, more than one line's budget (2.5 s), within two lines' (4.5 s), and every write
// ends 0.5 s before the worker's write timeout - no test-only hook in the production code.
TEST_F(FakeJLinkCli, CommandLinesWrittenAfter1500MsEachSucceed) {
    useScenario(R"({"probes": [{"serialNumber": 9, "rtt": {
        "acceptEachWriteAfterMs": 1500,
        "responses": {"version": "Firmware version: v3.1.0\n"}}}]})");
    const RunResult result = hio(
        {QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
         QStringLiteral("command"), QStringLiteral("version\nversion"), QStringLiteral("--timeout"),
         QStringLiteral("1"), QStringLiteral("--console-file"), QString()});
    EXPECT_EQ(result.exitCode, 0) << result.out << result.err;
    const auto all = envelopes(result.out);
    ASSERT_FALSE(all.empty()) << result.out;
    const QJsonArray output = data(all.at(0)).value(QStringLiteral("output")).toArray();
    ASSERT_EQ(output.size(), 2) << result.out;
    EXPECT_EQ(output.at(1).toString(), QStringLiteral("Firmware version: v3.1.0"));
}

// Review R39: more lines than the command queue holds (256) are sent while the event loop runs,
// retrying on internal.queue_full; every line is written and answered. Each write is accepted
// only at its second attempt (about 5 ms later), so the queue fills up.
TEST_F(FakeJLinkCli, CommandWith300LinesSucceeds) {
    useScenario(R"({"probes": [{"serialNumber": 10, "rtt": {
        "acceptEachWriteAfterMs": 1,
        "responses": {"version": "Firmware version: v3.1.0\n"}}}]})");
    QStringList commandLines;
    for (int i = 0; i < 300; ++i) {
        commandLines.append(QStringLiteral("version"));
    }
    const RunResult result =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("command"), commandLines.join(QLatin1Char('\n')),
             QStringLiteral("--timeout"), QStringLiteral("0.5"), QStringLiteral("--console-file"),
             QString()});
    EXPECT_EQ(result.exitCode, 0) << result.out << result.err;
    const auto all = envelopes(result.out);
    ASSERT_EQ(all.size(), 1U) << result.out;
    const QJsonArray output = data(all.at(0)).value(QStringLiteral("output")).toArray();
    EXPECT_EQ(output.size(), 300) << result.out;
}

// Review R42: the response of `command` is bounded (10 000 lines, output + log, 4 MiB). A reply of
// 12 000 lines keeps the first 10 000, "truncated" is true, and the collection ends at the bound
// instead of waiting for the 30 s timeout. Human mode prints the lines and a note on stderr.
TEST_F(FakeJLinkCli, CommandOutputIsTruncatedAfter10000Lines) {
    QString flood;
    for (int i = 0; i < 12000; ++i) {
        flood += QStringLiteral("line %1\n").arg(i);
    }
    const QJsonObject scenario{
        {QStringLiteral("probes"),
         QJsonArray{QJsonObject{
             {QStringLiteral("serialNumber"), 15},
             {QStringLiteral("rtt"),
              QJsonObject{
                  {QStringLiteral("up"),
                   QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("Terminal")},
                                          {QStringLiteral("size"), 4096}},
                              QJsonObject{{QStringLiteral("name"), QStringLiteral("Logger")},
                                          {QStringLiteral("size"), 1024}}}},
                  {QStringLiteral("responses"),
                   QJsonObject{{QStringLiteral("flood"), flood}}}}}}}}};
    useScenario(QJsonDocument(scenario).toJson(QJsonDocument::Compact).constData());
    const QStringList args{QStringLiteral("chester"),        QStringLiteral("app"),
                           QStringLiteral("command"),        QStringLiteral("flood"),
                           QStringLiteral("--timeout"),      QStringLiteral("30"),
                           QStringLiteral("--console-file"), QString()};

    const auto started = std::chrono::steady_clock::now();
    const RunResult json = hio(QStringList{QStringLiteral("--json")} + args);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(25));
    EXPECT_EQ(json.exitCode, 0) << json.err;
    const auto all = envelopes(json.out);
    ASSERT_EQ(all.size(), 1U) << json.err;
    const QJsonObject response = data(all.at(0));
    EXPECT_TRUE(response.value(QStringLiteral("truncated")).toBool());
    const QJsonArray output = response.value(QStringLiteral("output")).toArray();
    ASSERT_EQ(output.size() + response.value(QStringLiteral("log")).toArray().size(), 10000);
    EXPECT_EQ(output.at(0).toString(), QStringLiteral("line 0"));
    EXPECT_EQ(output.at(output.size() - 1).toString(),
              QStringLiteral("line %1").arg(output.size() - 1));

    const RunResult human = hio(args);
    EXPECT_EQ(human.exitCode, 0) << human.err;
    EXPECT_NE(human.err.find("Output truncated after 10000 lines"), std::string::npos) << human.err;
    EXPECT_EQ(std::count(human.out.begin(), human.out.end(), '\n'), 10000);
}

// Review R16: a command that cannot be written (down buffer of size 0) fails instead of exit 0.
TEST_F(FakeJLinkCli, CommandThatCannotBeWrittenFails) {
    useScenario(R"({"probes": [{"serialNumber": 8, "rtt": {
        "down": [{"name": "Terminal", "size": 0}]}}]})");
    const RunResult result =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("command"), QStringLiteral("version"), QStringLiteral("--console-file"),
             QString()});
    EXPECT_EQ(result.exitCode, 6) << result.out; // target category (ADR-0024)
    EXPECT_EQ(errorCode(result), QStringLiteral("target.rtt_buffer_not_found"));
}

// Decided 2026-10-09: --linger S collects output for S seconds after the end of input. The late
// line is the reply to the piped command, 1 s after its write (relative to the observed echo, not
// to boot; review R30): --linger 0 ends at the echo, --linger 4 waits long enough.
TEST_F(FakeJLinkCli, ConsoleLingerCollectsLateOutput) {
    useScenario(R"({"probes": [{"serialNumber": 6, "rtt": {
        "responseDelayMs": 1000, "responses": {"status": "late line\n"}}}]})");
    const auto hasLateLine = [](const std::string& out) {
        return out.find("late line") != std::string::npos;
    };
    const auto hasEcho = [](const std::string& out) {
        return std::ranges::any_of(envelopes(out), [](const QJsonObject& envelope) {
            const QJsonObject event = data(envelope);
            return event.value(QStringLiteral("dir")) == QStringLiteral("in") &&
                   event.value(QStringLiteral("text")) == QStringLiteral("status");
        });
    };
    const RunResult quick =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("console"), QStringLiteral("--linger"), QStringLiteral("0"),
             QStringLiteral("--console-file"), QString()},
            "status\n");
    EXPECT_EQ(quick.exitCode, 0) << quick.err;
    EXPECT_TRUE(hasEcho(quick.out)) << quick.out;
    EXPECT_FALSE(hasLateLine(quick.out)) << quick.out;
    const RunResult lingering =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("console"), QStringLiteral("--linger"), QStringLiteral("4"),
             QStringLiteral("--console-file"), QString()},
            "status\n");
    EXPECT_EQ(lingering.exitCode, 0) << lingering.err;
    EXPECT_TRUE(hasEcho(lingering.out)) << lingering.out;
    EXPECT_TRUE(hasLateLine(lingering.out)) << lingering.out;
    const RunResult invalid =
        hio({QStringLiteral("chester"), QStringLiteral("app"), QStringLiteral("console"),
             QStringLiteral("--linger"), QStringLiteral("61")});
    EXPECT_EQ(invalid.exitCode, 2);
}

// Decided 2026-10-09 (rttt): with --auto-reconnect a first attempt without power does not end the
// console; it reports Disconnected and connects once the target is powered.
TEST_F(FakeJLinkCli, ConsoleWithAutoReconnectStaysOpenWithoutPower) {
    useScenario(R"({"probes": [{"serialNumber": 7, "vtref": [[0, 150], [1500, 3300]]}]})");
    const RunResult result = hio(
        {QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
         QStringLiteral("console"), QStringLiteral("--auto-reconnect"), QStringLiteral("--linger"),
         QStringLiteral("6"), QStringLiteral("--console-file"), QString()});
    EXPECT_EQ(result.exitCode, 0) << result.err;
    std::vector<QString> states;
    for (const QJsonObject& envelope : envelopes(result.out)) {
        if (data(envelope).value(QStringLiteral("type")) == QStringLiteral("connection")) {
            states.push_back(data(envelope).value(QStringLiteral("state")).toString());
        }
    }
    ASSERT_GE(states.size(), 3U) << result.out;
    EXPECT_EQ(states[1], QStringLiteral("disconnected"));
    EXPECT_NE(std::ranges::find(states, QStringLiteral("connected")), states.end()) << result.out;
}

#ifndef _WIN32
// Starts `hio ... console` with an open stdin; returns when it reports "connected".
class RunningConsole {
public:
    RunningConsole(const QProcessEnvironment& env, const QStringList& args) {
        m_process.setProcessEnvironment(env);
        m_process.start(QStringLiteral(HIO_CLI_EXECUTABLE), args);
    }
    RunningConsole(const RunningConsole&) = delete;
    RunningConsole& operator=(const RunningConsole&) = delete;
    RunningConsole(RunningConsole&&) = delete;
    RunningConsole& operator=(RunningConsole&&) = delete;
    ~RunningConsole() {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            m_process.waitForFinished(5000);
        }
    }

    [[nodiscard]] bool waitFor(const QByteArray& text) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!m_err.contains(text) && std::chrono::steady_clock::now() < deadline) {
            m_process.waitForReadyRead(100);
            m_err += m_process.readAllStandardError();
        }
        return m_err.contains(text);
    }

    [[nodiscard]] bool waitForOut(const QByteArray& text) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!m_out.contains(text) && std::chrono::steady_clock::now() < deadline) {
            m_process.waitForReadyRead(100);
            m_out += m_process.readAllStandardOutput();
        }
        return m_out.contains(text);
    }

    QProcess& process() { return m_process; }
    QByteArray& err() { return m_err; }
    QByteArray& out() { return m_out; }

private:
    QProcess m_process;
    QByteArray m_err;
    QByteArray m_out;
};

TEST_F(FakeJLinkCli, CtrlCEndsTheConsoleWith130) {
    RunningConsole console(fakeEnvironment(), {QStringLiteral("chester"), QStringLiteral("app"),
                                               QStringLiteral("console"),
                                               QStringLiteral("--console-file"), QString()});
    ASSERT_TRUE(console.waitFor("-- connected --")) << console.err().toStdString();
    ASSERT_EQ(::kill(static_cast<int>(console.process().processId()), SIGINT), 0);
    ASSERT_TRUE(console.process().waitForFinished(10000)) << "hio did not stop after Ctrl+C";
    EXPECT_EQ(console.process().exitStatus(), QProcess::NormalExit);
    EXPECT_EQ(console.process().exitCode(), 130);
}

TEST_F(FakeJLinkCli, SecondConsoleOnTheSameProbeIsRefused) {
    RunningConsole first(fakeEnvironment(), {QStringLiteral("chester"), QStringLiteral("app"),
                                             QStringLiteral("console"), QStringLiteral("-n"),
                                             QStringLiteral("801000001"),
                                             QStringLiteral("--console-file"), QString()});
    ASSERT_TRUE(first.waitFor("-- connected --")) << first.err().toStdString();

    const RunResult second =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("console"), QStringLiteral("-n"), QStringLiteral("801000001")});
    EXPECT_EQ(second.exitCode, 5);
    EXPECT_EQ(errorCode(second), QStringLiteral("probe.in_use_by_other_process"));

    // `jlink list` names the owner; without -n a console takes the next free probe.
    const RunResult list =
        hio({QStringLiteral("--json"), QStringLiteral("jlink"), QStringLiteral("list")});
    EXPECT_EQ(list.exitCode, 0);
    const QJsonArray probes =
        data(envelopes(list.out).at(0)).value(QStringLiteral("probes")).toArray();
    ASSERT_EQ(probes.size(), 2);
    const QJsonObject owner = probes.at(0).toObject().value(QStringLiteral("inUseBy")).toObject();
    EXPECT_EQ(owner.value(QStringLiteral("app")).toString(), QStringLiteral("hio"));
    EXPECT_EQ(owner.value(QStringLiteral("pid")).toInteger(), first.process().processId());
    EXPECT_TRUE(probes.at(1).toObject().value(QStringLiteral("inUseBy")).isNull());

    const RunResult other =
        hio({QStringLiteral("chester"), QStringLiteral("app"), QStringLiteral("command"),
             QStringLiteral("version"), QStringLiteral("--timeout"), QStringLiteral("0.2")});
    EXPECT_EQ(other.exitCode, 0) << other.err;
    EXPECT_NE(other.err.find("Using J-Link 801000002"), std::string::npos) << other.err;
}

// Review R32: `console --json` whose reader goes away after the first line (`| head -n 1`):
// SIGPIPE is ignored, the failed write stops the console like Ctrl+C - exit 130 (cancelled), not
// death by signal (141), within a bounded time - and the console file gets its trailer.
TEST_F(FakeJLinkCli, ConsoleJsonEndsWhenTheReaderCloses) {
    QJsonArray output;
    for (int i = 0; i < 600; ++i) { // a line every 50 ms for 30 s
        output.append(QJsonObject{{QStringLiteral("atMs"), i * 50},
                                  {QStringLiteral("buffer"), 0},
                                  {QStringLiteral("text"), QStringLiteral("tick %1\n").arg(i)}});
    }
    const QJsonObject scenario{
        {QStringLiteral("probes"),
         QJsonArray{QJsonObject{
             {QStringLiteral("serialNumber"), 12},
             {QStringLiteral("rtt"), QJsonObject{{QStringLiteral("output"), output}}}}}}};
    useScenario(QJsonDocument(scenario).toJson(QJsonDocument::Compact).constData());
    const QString consoleFile = home() + QStringLiteral("/console.log");
    QProcess shell;
    shell.setProcessEnvironment(fakeEnvironment());
    // stdin stays open (the console would run forever); the exit status is hio's.
    shell.start(QStringLiteral("bash"),
                {QStringLiteral("-c"),
                 QStringLiteral(R"("$0" "$@" | head -n 1; exit "${PIPESTATUS[0]}")"),
                 QStringLiteral(HIO_CLI_EXECUTABLE), QStringLiteral("--json"),
                 QStringLiteral("chester"), QStringLiteral("app"), QStringLiteral("console"),
                 QStringLiteral("--console-file"), consoleFile});
    ASSERT_TRUE(shell.waitForStarted(10000));
    const auto started = std::chrono::steady_clock::now();
    const bool finished = shell.waitForFinished(30000);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (!finished) {
        shell.kill();
        shell.waitForFinished(5000);
    }
    ASSERT_TRUE(finished) << "hio did not stop after its reader closed";
    EXPECT_EQ(shell.exitStatus(), QProcess::NormalExit);
    EXPECT_EQ(shell.exitCode(), 130) << shell.readAllStandardError().toStdString();
    EXPECT_LT(elapsed, std::chrono::seconds(20));
    const auto lines = envelopes(shell.readAllStandardOutput().toStdString());
    ASSERT_EQ(lines.size(), 1U); // head printed the first line only
    QFile file(consoleFile);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray log = file.readAll();
    EXPECT_TRUE(log.trimmed().endsWith(" @ stopped")) << log.right(300).toStdString();
}

// Review R44: in human mode the closed stdout is reported on stderr with its own message
// ("The output was closed.", code `cancelled`, exit 130); the detail names the cause. It is
// detected on the first write after the reader went away.
TEST_F(FakeJLinkCli, ConsoleReportsTheClosedOutputWithItsOwnMessage) {
    QJsonArray output;
    for (int i = 0; i < 600; ++i) { // a line every 50 ms for 30 s
        output.append(QJsonObject{{QStringLiteral("atMs"), i * 50},
                                  {QStringLiteral("buffer"), 0},
                                  {QStringLiteral("text"), QStringLiteral("tick %1\n").arg(i)}});
    }
    const QJsonObject scenario{
        {QStringLiteral("probes"),
         QJsonArray{QJsonObject{
             {QStringLiteral("serialNumber"), 14},
             {QStringLiteral("rtt"), QJsonObject{{QStringLiteral("output"), output}}}}}}};
    useScenario(QJsonDocument(scenario).toJson(QJsonDocument::Compact).constData());
    QProcess shell;
    shell.setProcessEnvironment(fakeEnvironment());
    shell.start(QStringLiteral("bash"),
                {QStringLiteral("-c"),
                 QStringLiteral(R"("$0" "$@" | head -n 1 >/dev/null; exit "${PIPESTATUS[0]}")"),
                 QStringLiteral(HIO_CLI_EXECUTABLE), QStringLiteral("chester"),
                 QStringLiteral("app"), QStringLiteral("console"), QStringLiteral("--console-file"),
                 QString()});
    ASSERT_TRUE(shell.waitForStarted(10000));
    const bool finished = shell.waitForFinished(30000);
    if (!finished) {
        shell.kill();
        shell.waitForFinished(5000);
    }
    ASSERT_TRUE(finished) << "hio did not stop after its reader closed";
    const std::string err = shell.readAllStandardError().toStdString();
    EXPECT_EQ(shell.exitCode(), 130) << err;
    EXPECT_NE(err.find("Error: The output was closed."), std::string::npos) << err;
    EXPECT_EQ(err.find("Cancelled by user."), std::string::npos) << err;
    EXPECT_NE(err.find("the standard output was closed"), std::string::npos) << err;
}

// Review R34: Ctrl+C while the --json console is still connecting (a slow connect, no RTT
// control block): exactly one terminal "stopped" event, then the error envelope, exit 130.
TEST_F(FakeJLinkCli, CtrlCWhileConnectingEmitsOneStoppedEvent) {
    useScenario(R"({"probes": [{"serialNumber": 13, "connectDelayMs": 300,
                                "rtt": {"foundAfterMs": 600000}}]})");
    RunningConsole console(fakeEnvironment(), {QStringLiteral("--json"), QStringLiteral("chester"),
                                               QStringLiteral("app"), QStringLiteral("console"),
                                               QStringLiteral("--console-file"), QString()});
    ASSERT_TRUE(console.waitForOut("\"connecting\"")) << console.out().toStdString();
    ASSERT_EQ(::kill(static_cast<int>(console.process().processId()), SIGINT), 0);
    ASSERT_TRUE(console.process().waitForFinished(10000)) << "hio did not stop after Ctrl+C";
    EXPECT_EQ(console.process().exitStatus(), QProcess::NormalExit);
    EXPECT_EQ(console.process().exitCode(), 130);
    console.out() += console.process().readAllStandardOutput();
    const std::string out = console.out().toStdString();
    EXPECT_EQ(stoppedEvents(out), 1) << out;
    const auto all = envelopes(out);
    ASSERT_GE(all.size(), 2U) << out;
    EXPECT_FALSE(all.back().value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(all.back().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")),
              QStringLiteral("cancelled"));
    EXPECT_EQ(data(all.at(all.size() - 2)).value(QStringLiteral("state")),
              QStringLiteral("stopped"));
}
#endif

} // namespace
