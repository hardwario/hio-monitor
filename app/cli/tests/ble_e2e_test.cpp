// End-to-end tests of `hio ble scan|console|command|pair` (plan/features/0011 C3) with the fake
// BLE backend (ADR-0087, ADR-0127: HIO_FAKE_BLE_SCENARIO, builds with HIO_TEST_OVERRIDES only)
// and the shared scenarios in tests/ble_scenarios.

#include "cli_process.h"

#include <QByteArray>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <chrono>
#include <qcontainerfwd.h>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <csignal>
#endif

namespace {

using hio::cli::test::CliProcess;
using hio::cli::test::RunResult;
using namespace std::chrono_literals;

const QString device = QStringLiteral("F1:22:33:44:55:66");

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

class FakeBleCli : public CliProcess {
protected:
    [[nodiscard]] QProcessEnvironment scenario(const char* name) const {
        QProcessEnvironment env = environment();
        env.insert(QStringLiteral("HIO_FAKE_BLE_SCENARIO"), QStringLiteral(HIO_BLE_SCENARIOS_DIR) +
                                                                QStringLiteral("/") +
                                                                QString::fromLatin1(name));
        return env;
    }
    [[nodiscard]] RunResult hio(const char* name, const QStringList& args,
                                const QByteArray& input = {}) const {
        return run(args, scenario(name), input);
    }
};

TEST_F(FakeBleCli, ScanListsProfileDevicesByRssi) {
    const RunResult result = hio(
        "chester.json", {QStringLiteral("--json"), QStringLiteral("ble"), QStringLiteral("scan"),
                         QStringLiteral("--timeout"), QStringLiteral("1")});
    ASSERT_EQ(result.exitCode, 0) << result.err;
    const QJsonObject payload = data(envelopes(result.out).at(0));
    EXPECT_EQ(payload.value(QStringLiteral("adapter")).toObject().value(QStringLiteral("name")),
              QStringLiteral("fake0"));
    const QJsonArray devices = payload.value(QStringLiteral("devices")).toArray();
    ASSERT_EQ(devices.size(), 2);
    const QJsonObject first = devices.at(0).toObject();
    EXPECT_EQ(first.value(QStringLiteral("id")).toString(), device);
    EXPECT_EQ(first.value(QStringLiteral("name")).toString(), QStringLiteral("CHESTER-ABC123"));
    EXPECT_EQ(first.value(QStringLiteral("rssi")).toInt(), -58);
    EXPECT_TRUE(first.value(QStringLiteral("nusAdvertised")).toBool());
    EXPECT_EQ(first.value(QStringLiteral("profile")).toString(), QStringLiteral("chester"));
    EXPECT_TRUE(first.value(QStringLiteral("inUseBy")).isNull());

    const RunResult all = hio("chester.json", {QStringLiteral("--json"), QStringLiteral("ble"),
                                               QStringLiteral("scan"), QStringLiteral("--timeout"),
                                               QStringLiteral("1"), QStringLiteral("--all")});
    const QJsonArray every =
        data(envelopes(all.out).at(0)).value(QStringLiteral("devices")).toArray();
    ASSERT_EQ(every.size(), 4);
    EXPECT_TRUE(every.at(0).toObject().value(QStringLiteral("profile")).isNull());
    // ADR-0142: a device that only advertises NUS has no profile; it is listed with --all only.
    const QJsonObject ruuvi = every.at(2).toObject();
    EXPECT_EQ(ruuvi.value(QStringLiteral("name")).toString(), QStringLiteral("Ruuvi 1234"));
    EXPECT_TRUE(ruuvi.value(QStringLiteral("nusAdvertised")).toBool());
    EXPECT_TRUE(ruuvi.value(QStringLiteral("profile")).isNull());

    const RunResult human = hio("chester.json", {QStringLiteral("ble"), QStringLiteral("scan"),
                                                 QStringLiteral("--timeout"), QStringLiteral("1")});
    EXPECT_NE(human.out.find("CHESTER-ABC123"), std::string::npos) << human.out;
    EXPECT_NE(human.out.find("2 devices (1 s scan). Use --all"), std::string::npos) << human.out;
}

TEST_F(FakeBleCli, ScanWithoutDevicesIsAnEmptyList) {
    const RunResult result =
        hio("empty.json", {QStringLiteral("--json"), QStringLiteral("ble"), QStringLiteral("scan"),
                           QStringLiteral("--timeout"), QStringLiteral("1")});
    ASSERT_EQ(result.exitCode, 0) << result.err;
    EXPECT_TRUE(
        data(envelopes(result.out).at(0)).value(QStringLiteral("devices")).toArray().isEmpty());
}

TEST_F(FakeBleCli, ScanListIsBoundedAt256) {
    const RunResult result =
        hio("many.json", {QStringLiteral("--json"), QStringLiteral("ble"), QStringLiteral("scan"),
                          QStringLiteral("--timeout"), QStringLiteral("1")});
    ASSERT_EQ(result.exitCode, 0) << result.err;
    EXPECT_EQ(data(envelopes(result.out).at(0)).value(QStringLiteral("devices")).toArray().size(),
              256);
}

TEST_F(FakeBleCli, AdapterProblemsAreDependencyErrors) {
    for (const auto& [file, code] :
         {std::pair{"adapter_off.json", "ble.adapter_off"},
          std::pair{"permission_denied.json", "ble.permission_denied"},
          std::pair{"no_adapter.json", "ble.adapter_not_found"},
          std::pair{"stack_unavailable.json", "ble.stack_unavailable"}}) {
        const RunResult result =
            hio(file, {QStringLiteral("--json"), QStringLiteral("ble"), QStringLiteral("scan")});
        EXPECT_EQ(result.exitCode, 4) << file;
        EXPECT_EQ(errorCode(result), QString::fromLatin1(code)) << file;
    }
}

TEST_F(FakeBleCli, CommandAnswersAndEndsAtThePrompt) {
    const auto started = std::chrono::steady_clock::now();
    const RunResult result = hio(
        "chester.json", {QStringLiteral("--json"), QStringLiteral("ble"), QStringLiteral("command"),
                         QStringLiteral("version"), QStringLiteral("-a"), device,
                         QStringLiteral("-t"), QStringLiteral("20")});
    const auto elapsed = std::chrono::steady_clock::now() - started;
    ASSERT_EQ(result.exitCode, 0) << result.err;
    const QJsonObject payload = data(envelopes(result.out).at(0));
    EXPECT_EQ(payload.value(QStringLiteral("command")).toString(), QStringLiteral("version"));
    EXPECT_EQ(payload.value(QStringLiteral("output")).toArray(),
              QJsonArray({QStringLiteral("Firmware version: v3.1.0")}));
    EXPECT_FALSE(payload.value(QStringLiteral("truncated")).toBool());
    EXPECT_LT(elapsed, 15s) << "the prompt did not end the collection";
}

TEST_F(FakeBleCli, CommandWithoutPromptEndsByTheTimeout) {
    const auto started = std::chrono::steady_clock::now();
    const RunResult result =
        hio("no_prompt.json",
            {QStringLiteral("ble"), QStringLiteral("command"), QStringLiteral("version"),
             QStringLiteral("-a"), device, QStringLiteral("-t"), QStringLiteral("1")});
    ASSERT_EQ(result.exitCode, 0) << result.err;
    EXPECT_EQ(result.out, "Firmware version: v3.1.0\n");
    EXPECT_GE(std::chrono::steady_clock::now() - started, 1s);
}

TEST_F(FakeBleCli, CommandByNameAndSmallMtu) {
    const RunResult result =
        hio("small_mtu.json",
            {QStringLiteral("ble"), QStringLiteral("command"), QStringLiteral("version"),
             QStringLiteral("--name"), QStringLiteral("abc123")});
    ASSERT_EQ(result.exitCode, 0) << result.err;
    // The device echoes (with-response chunks of 20 bytes, write-without-response here).
    EXPECT_NE(result.out.find("Firmware version: v3.1.0"), std::string::npos) << result.out;
}

TEST_F(FakeBleCli, AmbiguousNameIsAUsageError) {
    const RunResult result =
        hio("chester.json",
            {QStringLiteral("--json"), QStringLiteral("ble"), QStringLiteral("command"),
             QStringLiteral("version"), QStringLiteral("--name"), QStringLiteral("chester")});
    EXPECT_EQ(result.exitCode, 2);
    EXPECT_EQ(errorCode(result), QStringLiteral("usage.invalid_argument"));
    EXPECT_NE(result.out.find("F1:22:33:44:55:66"), std::string::npos);
    EXPECT_NE(result.out.find("C0:11:22:33:44:55"), std::string::npos);
}

TEST_F(FakeBleCli, UnknownNameIsDeviceNotFound) {
    const RunResult result = hio(
        "chester.json", {QStringLiteral("--json"), QStringLiteral("ble"), QStringLiteral("console"),
                         QStringLiteral("--name"), QStringLiteral("nothing")});
    EXPECT_EQ(result.exitCode, 6);
    EXPECT_EQ(errorCode(result), QStringLiteral("ble.device_not_found"));
}

TEST_F(FakeBleCli, NotPairedIsATargetError) {
    const RunResult result =
        hio("not_paired.json",
            {QStringLiteral("--json"), QStringLiteral("ble"), QStringLiteral("command"),
             QStringLiteral("version"), QStringLiteral("-a"), device});
    EXPECT_EQ(result.exitCode, 6);
    EXPECT_EQ(errorCode(result), QStringLiteral("ble.not_paired"));
}

TEST_F(FakeBleCli, StalledWriteIsAWriteTimeout) {
    const RunResult result =
        hio("write_stall.json",
            {QStringLiteral("--json"), QStringLiteral("ble"), QStringLiteral("command"),
             QStringLiteral("version"), QStringLiteral("-a"), device});
    EXPECT_EQ(result.exitCode, 6);
    EXPECT_EQ(errorCode(result), QStringLiteral("ble.write_timeout"));
}

TEST_F(FakeBleCli, PairingPromptIsShown) {
    const RunResult result =
        hio("pairing_prompt.json", {QStringLiteral("ble"), QStringLiteral("command"),
                                    QStringLiteral("version"), QStringLiteral("-a"), device});
    ASSERT_EQ(result.exitCode, 0) << result.err;
    EXPECT_NE(result.err.find("Pairing: enter the passkey in the system dialog. The passkey is "
                              "123456 unless it was changed when the device was configured."),
              std::string::npos)
        << result.err;
    EXPECT_EQ(result.out, "Firmware version: v3.1.0\n");
}

TEST_F(FakeBleCli, ConsoleJsonWithPipedInputAndAutoReconnect) {
    // drop.json: the link is lost 300 ms after connecting; --auto-reconnect connects again while
    // the device is seen (background scan).
    const RunResult result =
        run({QStringLiteral("--json"), QStringLiteral("ble"), QStringLiteral("console"),
             QStringLiteral("-a"), device, QStringLiteral("--auto-reconnect"),
             QStringLiteral("--console-file"), QString()},
            scenario("drop.json"), QByteArray());
    // stdin ends at once: the console ends after the linger; the drop happens meanwhile or not,
    // so this checks the stream shape only.
    ASSERT_EQ(result.exitCode, 0) << result.err;
    const auto all = envelopes(result.out);
    ASSERT_GE(all.size(), 3U) << result.out;
    const QJsonObject connected = data(all.at(1));
    EXPECT_EQ(connected.value(QStringLiteral("state")).toString(), QStringLiteral("connected"));
    EXPECT_EQ(connected.value(QStringLiteral("transport")).toString(), QStringLiteral("ble"));
    EXPECT_EQ(connected.value(QStringLiteral("mtu")).toInt(), 247);
    const QJsonObject stopped = data(all.back());
    EXPECT_EQ(stopped.value(QStringLiteral("state")).toString(), QStringLiteral("stopped"));
    EXPECT_EQ(stopped.value(QStringLiteral("transport")).toString(), QStringLiteral("ble"));
}

TEST_F(FakeBleCli, ConsoleWritesTheDefaultConsoleFile) {
    const RunResult result =
        hio("chester.json",
            {QStringLiteral("ble"), QStringLiteral("console"), QStringLiteral("-a"), device},
            QByteArray("version\n"));
    ASSERT_EQ(result.exitCode, 0) << result.err;
    EXPECT_NE(result.out.find("Firmware version: v3.1.0"), std::string::npos) << result.out;
    const QDir console(home() + QStringLiteral("/data/HARDWARIO/Monitor/console"));
    const QStringList files =
        console.entryList({QStringLiteral("chester-ble-*-F1-22-33-44-55-66.log")}, QDir::Files);
    EXPECT_EQ(files.size(), 1) << console.absolutePath().toStdString();
}

TEST_F(FakeBleCli, PairOutcomes) {
    const RunResult paired =
        hio("unpaired.json", {QStringLiteral("--json"), QStringLiteral("ble"),
                              QStringLiteral("pair"), QStringLiteral("-a"), device});
    ASSERT_EQ(paired.exitCode, 0) << paired.err;
    const QJsonObject payload = data(envelopes(paired.out).at(0));
    EXPECT_EQ(payload.value(QStringLiteral("id")).toString(), device);
    EXPECT_TRUE(payload.value(QStringLiteral("paired")).toBool());
    const RunResult human = hio("chester.json", {QStringLiteral("ble"), QStringLiteral("pair"),
                                                 QStringLiteral("-a"), device});
    EXPECT_EQ(human.out, "Already paired.\n");
    const RunResult failed = hio("unpaired.json", {QStringLiteral("--json"), QStringLiteral("ble"),
                                                   QStringLiteral("pair"), QStringLiteral("-a"),
                                                   QStringLiteral("C0:11:22:33:44:55")});
    EXPECT_EQ(failed.exitCode, 6);
    EXPECT_EQ(errorCode(failed), QStringLiteral("ble.pairing_failed"));
    const RunResult macos =
        hio("macos.json",
            {QStringLiteral("--json"), QStringLiteral("ble"), QStringLiteral("pair"),
             QStringLiteral("-a"), QStringLiteral("6A0B3F1C-1D2E-4F50-8A9B-0C1D2E3F4A5B")});
    EXPECT_EQ(macos.exitCode, 2);
    EXPECT_EQ(errorCode(macos), QStringLiteral("ble.pairing_not_supported"));
}

// Review B4: more lines than may wait at once are paced, none is lost.
TEST_F(FakeBleCli, CommandWithManyLinesWritesThemAll) {
    QString many;
    for (int i = 0; i < 300; ++i) {
        many += QStringLiteral("version\n");
    }
    const RunResult result =
        hio("chester.json",
            {QStringLiteral("--json"), QStringLiteral("ble"), QStringLiteral("command"), many,
             QStringLiteral("-a"), device, QStringLiteral("-t"), QStringLiteral("5")});
    ASSERT_EQ(result.exitCode, 0) << result.err;
    const QJsonArray output =
        data(envelopes(result.out).at(0)).value(QStringLiteral("output")).toArray();
    EXPECT_EQ(output.size(), 300);
}

// Review B6: another spelling of the address is the same device.
TEST_F(FakeBleCli, LowerCaseAddressIsTheSameDevice) {
    const RunResult command = hio("chester.json", {QStringLiteral("ble"), QStringLiteral("command"),
                                                   QStringLiteral("version"), QStringLiteral("-a"),
                                                   QStringLiteral("f1:22:33:44:55:66")});
    ASSERT_EQ(command.exitCode, 0) << command.err;
    EXPECT_EQ(command.out, "Firmware version: v3.1.0\n");
    const RunResult pair = hio("unpaired.json", {QStringLiteral("--json"), QStringLiteral("ble"),
                                                 QStringLiteral("pair"), QStringLiteral("-a"),
                                                 QStringLiteral("f1:22:33:44:55:66")});
    ASSERT_EQ(pair.exitCode, 0) << pair.err;
    EXPECT_EQ(data(envelopes(pair.out).at(0)).value(QStringLiteral("id")).toString(), device);
}

// Review B8: an undecided permission is asked for before the first scan (here the system answers
// at once) and the scan runs.
TEST_F(FakeBleCli, UndecidedPermissionIsAskedForFirst) {
    const RunResult result =
        hio("permission_undetermined.json",
            {QStringLiteral("--json"), QStringLiteral("ble"), QStringLiteral("scan"),
             QStringLiteral("--timeout"), QStringLiteral("1")});
    EXPECT_EQ(result.exitCode, 0) << result.out << result.err;
}

// Review N6: after the permission was granted the adapter is read again (here it is off).
TEST_F(FakeBleCli, AdapterIsReadAgainAfterThePermission) {
    const RunResult result =
        hio("permission_then_off.json",
            {QStringLiteral("--json"), QStringLiteral("ble"), QStringLiteral("scan"),
             QStringLiteral("--timeout"), QStringLiteral("1")});
    EXPECT_EQ(result.exitCode, 4) << result.out;
    EXPECT_EQ(errorCode(result), QStringLiteral("ble.adapter_off"));
}

// HIL H1: no pairing agent is a dependency problem (exit 4) with its own code.
TEST_F(FakeBleCli, PairWithoutAgent) {
    const RunResult result =
        hio("no_agent.json", {QStringLiteral("--json"), QStringLiteral("ble"),
                              QStringLiteral("pair"), QStringLiteral("-a"), device});
    EXPECT_EQ(result.exitCode, 4) << result.out;
    EXPECT_EQ(errorCode(result), QStringLiteral("ble.no_pairing_agent"));
}

// HIL H5: no empty first output line (the shell's new line after Enter).
TEST_F(FakeBleCli, CommandOutputHasNoLeadingEmptyLine) {
    const RunResult result = hio(
        "chester.json", {QStringLiteral("--json"), QStringLiteral("ble"), QStringLiteral("command"),
                         QStringLiteral("status"), QStringLiteral("-a"), device});
    ASSERT_EQ(result.exitCode, 0) << result.err;
    EXPECT_EQ(data(envelopes(result.out).at(0)).value(QStringLiteral("output")).toArray(),
              QJsonArray({QStringLiteral("line 1"), QStringLiteral("line 2")}));
}

// HIL H5: only the first empty line is the shell's; an empty output line later is kept.
TEST_F(FakeBleCli, EmptyLineInsideCommandOutputIsKept) {
    const RunResult result = hio(
        "chester.json", {QStringLiteral("--json"), QStringLiteral("ble"), QStringLiteral("command"),
                         QStringLiteral("lines"), QStringLiteral("-a"), device});
    ASSERT_EQ(result.exitCode, 0) << result.err;
    EXPECT_EQ(data(envelopes(result.out).at(0)).value(QStringLiteral("output")).toArray(),
              QJsonArray({QStringLiteral("line 1"), QString(), QStringLiteral("line 3")}));
}

TEST_F(FakeBleCli, DeviceIsRequired) {
    const RunResult result = hio("chester.json", {QStringLiteral("ble"), QStringLiteral("command"),
                                                  QStringLiteral("version")});
    EXPECT_EQ(result.exitCode, 2);
}

#ifndef _WIN32
// A console with an open stdin.
class RunningBleConsole {
public:
    RunningBleConsole(const QProcessEnvironment& env, const QStringList& args) {
        m_process.setProcessEnvironment(env);
        m_process.start(QStringLiteral(HIO_CLI_EXECUTABLE), args);
    }
    RunningBleConsole(const RunningBleConsole&) = delete;
    RunningBleConsole& operator=(const RunningBleConsole&) = delete;
    RunningBleConsole(RunningBleConsole&&) = delete;
    RunningBleConsole& operator=(RunningBleConsole&&) = delete;
    ~RunningBleConsole() {
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

    QProcess& process() { return m_process; }
    QByteArray& err() { return m_err; }

private:
    QProcess m_process;
    QByteArray m_err;
};

TEST_F(FakeBleCli, CtrlCEndsTheConsoleWith130) {
    RunningBleConsole console(scenario("chester.json"),
                              {QStringLiteral("ble"), QStringLiteral("console"),
                               QStringLiteral("-a"), device, QStringLiteral("--console-file"),
                               QString()});
    ASSERT_TRUE(console.waitFor("-- connected (Bluetooth, MTU 247) --"))
        << console.err().toStdString();
    ASSERT_EQ(::kill(static_cast<int>(console.process().processId()), SIGINT), 0);
    ASSERT_TRUE(console.process().waitForFinished(10000)) << "hio did not stop after Ctrl+C";
    EXPECT_EQ(console.process().exitStatus(), QProcess::NormalExit);
    EXPECT_EQ(console.process().exitCode(), 130);
}

TEST_F(FakeBleCli, SecondProcessOnTheSameDeviceIsRefused) {
    RunningBleConsole first(scenario("chester.json"),
                            {QStringLiteral("ble"), QStringLiteral("console"), QStringLiteral("-a"),
                             device, QStringLiteral("--console-file"), QString()});
    ASSERT_TRUE(first.waitFor("-- connected")) << first.err().toStdString();
    const RunResult second = hio(
        "chester.json", {QStringLiteral("--json"), QStringLiteral("ble"), QStringLiteral("command"),
                         QStringLiteral("version"), QStringLiteral("-a"), device});
    EXPECT_EQ(second.exitCode, 5);
    EXPECT_EQ(errorCode(second), QStringLiteral("ble.in_use_by_other_process"));
    // The scan names the owner.
    const RunResult scan = hio("chester.json", {QStringLiteral("--json"), QStringLiteral("ble"),
                                                QStringLiteral("scan"), QStringLiteral("--timeout"),
                                                QStringLiteral("1")});
    const QJsonObject owner = data(envelopes(scan.out).at(0))
                                  .value(QStringLiteral("devices"))
                                  .toArray()
                                  .at(0)
                                  .toObject()
                                  .value(QStringLiteral("inUseBy"))
                                  .toObject();
    EXPECT_EQ(owner.value(QStringLiteral("pid")).toInteger(), first.process().processId());
    const RunResult pair =
        hio("chester.json", {QStringLiteral("--json"), QStringLiteral("ble"),
                             QStringLiteral("pair"), QStringLiteral("-a"), device});
    EXPECT_EQ(pair.exitCode, 5);
}
#endif

} // namespace
