// End-to-end tests of the M4 diagnostics commands (plan/features/0007, B3; ADR-0070): `hio doctor`,
// `hio diag report` and `hio chester app|lte check` with the fake J-Link library and the fake
// nRF Util. All serial numbers are fake.

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
#include <QStringLiteral>
#include <QtContainerFwd>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <csignal>
#endif

namespace {

using hio::cli::test::CliProcess;
using hio::cli::test::RunResult;

constexpr const char* oneProbe = R"({"probes": [
    {"serialNumber": 801000001, "product": "Test Probe",
     "rtt": {"up": [{"name": "Terminal", "size": 1024}, {"name": "Logger", "size": 1024}],
             "down": [{"name": "Terminal", "size": 16}]}}]})";

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

QJsonArray checks(const QJsonObject& envelope) {
    return envelope.value(QStringLiteral("data"))
        .toObject()
        .value(QStringLiteral("checks"))
        .toArray();
}

QString statusOf(const QJsonArray& list, const QString& id) {
    for (const auto& value : list) {
        if (value.toObject().value(QStringLiteral("id")).toString() == id) {
            return value.toObject().value(QStringLiteral("status")).toString();
        }
    }
    return {};
}

class DiagCli : public CliProcess {
protected:
    void SetUp() override { useScenario(oneProbe); }

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
        env.insert(QStringLiteral("HIO_NRFUTIL"), QStringLiteral(HIO_FAKE_NRFUTIL));
        // M5: the `bluetooth` check on the fake BLE backend (HIO_TEST_OVERRIDES builds; others
        // read the real adapter, the check is a warning at worst).
        env.insert(QStringLiteral("HIO_FAKE_BLE_SCENARIO"),
                   QStringLiteral(HIO_BLE_SCENARIOS_DIR) + QStringLiteral("/") + m_bleScenario);
        return env;
    }

    // tests/ble_scenarios/<name> for the `bluetooth` check.
    void useBleScenario(QString name) { m_bleScenario = std::move(name); }

    [[nodiscard]] RunResult hio(const QStringList& args) const {
        return run(args, fakeEnvironment());
    }

private:
    QString m_bleScenario = QStringLiteral("chester.json");
};

TEST_F(DiagCli, DoctorJson) {
    const RunResult result = hio({QStringLiteral("--json"), QStringLiteral("doctor")});
    ASSERT_TRUE(result.normalExit);
    EXPECT_EQ(result.exitCode, 0) << result.out << result.err;
    const auto all = envelopes(result.out);
    ASSERT_EQ(all.size(), 1U);
    EXPECT_TRUE(all[0].value(QStringLiteral("ok")).toBool());
    const QJsonArray list = checks(all[0]);
    ASSERT_EQ(list.size(), 8);
    EXPECT_EQ(statusOf(list, QStringLiteral("jlink-software")), QStringLiteral("ok"));
    EXPECT_EQ(statusOf(list, QStringLiteral("nrfutil")), QStringLiteral("ok"));
    EXPECT_EQ(statusOf(list, QStringLiteral("probes")), QStringLiteral("ok"));
    EXPECT_EQ(statusOf(list, QStringLiteral("app-dirs")), QStringLiteral("ok"));
    const QJsonObject first = list.at(0).toObject();
    for (const char* key : {"id", "status", "title", "result", "error"}) {
        EXPECT_TRUE(first.contains(QString::fromLatin1(key))) << key;
    }
    EXPECT_TRUE(first.value(QStringLiteral("error")).isNull());
}

// M5 (ADR-0088): the bluetooth check; a Bluetooth problem is a warning and keeps exit 0.
TEST_F(DiagCli, DoctorBluetooth) {
#ifndef HIO_TEST_OVERRIDES
    GTEST_SKIP() << "needs HIO_TEST_OVERRIDES (HIO_FAKE_BLE_SCENARIO)";
#endif
    const RunResult ready = hio({QStringLiteral("--json"), QStringLiteral("doctor")});
    EXPECT_EQ(statusOf(checks(envelopes(ready.out).at(0)), QStringLiteral("bluetooth")),
              QStringLiteral("ok"));
    EXPECT_NE(ready.out.find("Adapter ready (fake0, 00:11:22:33:44:55)"), std::string::npos)
        << ready.out;
    useBleScenario(QStringLiteral("adapter_off.json"));
    const RunResult off = hio({QStringLiteral("--json"), QStringLiteral("doctor")});
    EXPECT_EQ(off.exitCode, 0) << off.out;
    EXPECT_EQ(statusOf(checks(envelopes(off.out).at(0)), QStringLiteral("bluetooth")),
              QStringLiteral("warning"));
    EXPECT_NE(off.out.find("ble.adapter_off"), std::string::npos) << off.out;
    useBleScenario(QStringLiteral("permission_undetermined.json"));
    const RunResult undecided = hio({QStringLiteral("doctor")});
    EXPECT_NE(undecided.out.find("Permission will be asked on first use"), std::string::npos)
        << undecided.out;
}

TEST_F(DiagCli, DoctorHuman) {
    const RunResult result = hio({QStringLiteral("doctor")});
    EXPECT_EQ(result.exitCode, 0) << result.err;
    EXPECT_NE(result.out.find("✓ J-Link Software"), std::string::npos) << result.out;
    EXPECT_NE(result.out.find("✓ J-Link probes"), std::string::npos) << result.out;
    EXPECT_NE(result.out.find("1 found: 801000001"), std::string::npos) << result.out;
    EXPECT_NE(result.out.find("Run `hio diag report` to create a report for HARDWARIO support."),
              std::string::npos)
        << result.out;
}

TEST_F(DiagCli, DoctorWithoutJLinkExitsWithTheDependencyCode) {
    const RunResult result =
        hio({QStringLiteral("--jlink-library"), QStringLiteral("/nonexistent/libjlinkarm.so"),
             QStringLiteral("--json"), QStringLiteral("doctor")});
    EXPECT_EQ(result.exitCode, 4) << result.out;
    const auto all = envelopes(result.out);
    ASSERT_EQ(all.size(), 1U);
    EXPECT_FALSE(all[0].value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(all[0].value(QStringLiteral("error")).toObject().value(QStringLiteral("code")),
              QStringLiteral("dependency.jlink_not_found"));
    const QJsonArray list = checks(all[0]); // ADR-0070: the full list also on failure
    ASSERT_EQ(list.size(), 8);
    EXPECT_EQ(statusOf(list, QStringLiteral("probes")), QStringLiteral("skipped"));

    const RunResult human =
        hio({QStringLiteral("--jlink-library"), QStringLiteral("/nonexistent/libjlinkarm.so"),
             QStringLiteral("doctor")});
    EXPECT_EQ(human.exitCode, 4);
    EXPECT_NE(human.out.find("✗ J-Link Software"), std::string::npos) << human.out;
    EXPECT_NE(human.out.find("What to do:"), std::string::npos) << human.out;
    EXPECT_NE(human.out.find("1 failed"), std::string::npos) << human.out;
}

TEST_F(DiagCli, DoctorWithoutNrfUtilOnlyWarns) {
    const RunResult result =
        hio({QStringLiteral("--nrfutil"), QStringLiteral("/nonexistent/nrfutil"),
             QStringLiteral("--json"), QStringLiteral("doctor")});
    EXPECT_EQ(result.exitCode, 0) << result.out;
    const auto all = envelopes(result.out);
    ASSERT_EQ(all.size(), 1U);
    EXPECT_EQ(statusOf(checks(all[0]), QStringLiteral("nrfutil")), QStringLiteral("warning"));
}

TEST_F(DiagCli, AppCheckStreamsEveryStep) {
    const RunResult result = hio({QStringLiteral("--json"), QStringLiteral("chester"),
                                  QStringLiteral("app"), QStringLiteral("check")});
    EXPECT_EQ(result.exitCode, 0) << result.out << result.err;
    const auto all = envelopes(result.out);
    ASSERT_EQ(all.size(), 7U) << result.out;
    const QStringList ids{QStringLiteral("probe"),   QStringLiteral("power"),
                          QStringLiteral("connect"), QStringLiteral("identify"),
                          QStringLiteral("rtt"),     QStringLiteral("rtt-buffers")};
    for (int i = 0; i < 6; ++i) {
        const QJsonObject event =
            all.at(static_cast<std::size_t>(i)).value(QStringLiteral("data")).toObject();
        EXPECT_EQ(event.value(QStringLiteral("type")), QStringLiteral("check"));
        EXPECT_EQ(event.value(QStringLiteral("id")), ids.at(i));
        EXPECT_EQ(event.value(QStringLiteral("status")), QStringLiteral("ok"))
            << ids.at(i).toStdString();
    }
    EXPECT_TRUE(all.back().value(QStringLiteral("ok")).toBool());
    EXPECT_EQ(checks(all.back()).size(), 6);

    const RunResult human =
        hio({QStringLiteral("chester"), QStringLiteral("app"), QStringLiteral("check")});
    EXPECT_EQ(human.exitCode, 0);
    EXPECT_NE(human.out.find("✓ Connection to the application MCU"), std::string::npos)
        << human.out;
    EXPECT_NE(human.out.find("All checks passed."), std::string::npos) << human.out;
}

TEST_F(DiagCli, AppCheckWithoutPowerExitsWithTheTargetCode) {
    useScenario(R"({"probes": [{"serialNumber": 801000001, "vtref": [[0, 150]]}]})");
    const RunResult result = hio({QStringLiteral("--json"), QStringLiteral("chester"),
                                  QStringLiteral("app"), QStringLiteral("check")});
    EXPECT_EQ(result.exitCode, 6) << result.out;
    const auto all = envelopes(result.out);
    ASSERT_FALSE(all.empty());
    EXPECT_EQ(all.back().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")),
              QStringLiteral("target.no_power"));
    EXPECT_EQ(statusOf(checks(all.back()), QStringLiteral("connect")), QStringLiteral("skipped"));
}

TEST_F(DiagCli, LteCheckOnTheAppConnectorIsWrongFamily) {
    const RunResult result = hio({QStringLiteral("--json"), QStringLiteral("chester"),
                                  QStringLiteral("lte"), QStringLiteral("check")});
    EXPECT_EQ(result.exitCode, 6) << result.out;
    const auto all = envelopes(result.out);
    ASSERT_FALSE(all.empty());
    EXPECT_EQ(all.back().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")),
              QStringLiteral("target.wrong_family"));
}

TEST_F(DiagCli, CheckWithoutProbeExitsWithTheProbeCode) {
    useScenario(R"({"probes": []})");
    const RunResult result = hio({QStringLiteral("--json"), QStringLiteral("chester"),
                                  QStringLiteral("app"), QStringLiteral("check")});
    EXPECT_EQ(result.exitCode, 5) << result.out;
    const auto all = envelopes(result.out);
    ASSERT_FALSE(all.empty());
    EXPECT_EQ(all.back().value(QStringLiteral("error")).toObject().value(QStringLiteral("code")),
              QStringLiteral("probe.not_found"));
}

TEST_F(DiagCli, ReportIsWritten) {
    const QString target = home() + QStringLiteral("/report.zip");
    const RunResult result = hio({QStringLiteral("--json"), QStringLiteral("diag"),
                                  QStringLiteral("report"), QStringLiteral("-o"), target});
    EXPECT_EQ(result.exitCode, 0) << result.out << result.err;
    const auto all = envelopes(result.out);
    ASSERT_EQ(all.size(), 1U);
    const QJsonObject data = all[0].value(QStringLiteral("data")).toObject();
    EXPECT_TRUE(
        data.value(QStringLiteral("path")).toString().endsWith(QStringLiteral("report.zip")));
    EXPECT_GT(data.value(QStringLiteral("bytes")).toInteger(), 0);
    QFile zip(target);
    ASSERT_TRUE(zip.open(QIODevice::ReadOnly));
    EXPECT_TRUE(zip.read(2) == QByteArray("PK"));

    const RunResult human = hio({QStringLiteral("diag"), QStringLiteral("report"),
                                 QStringLiteral("-o"), home() + QStringLiteral("/r2.zip")});
    EXPECT_EQ(human.exitCode, 0);
    EXPECT_NE(human.out.find("HARDWARIO Monitor diagnostic report"), std::string::npos)
        << human.out;
}

TEST_F(DiagCli, ReportToTheTerminalIsRefused) {
    const RunResult result =
        hio({QStringLiteral("--json"), QStringLiteral("diag"), QStringLiteral("report"),
             QStringLiteral("-o"), QStringLiteral("-")});
    EXPECT_EQ(result.exitCode, 2);
    const auto all = envelopes(result.out);
    ASSERT_EQ(all.size(), 1U);
    EXPECT_EQ(all[0].value(QStringLiteral("error")).toObject().value(QStringLiteral("code")),
              QStringLiteral("usage.invalid_argument"));
}

TEST_F(DiagCli, ReportIntoAMissingFolderFails) {
    const RunResult result =
        hio({QStringLiteral("--json"), QStringLiteral("diag"), QStringLiteral("report"),
             QStringLiteral("-o"), home() + QStringLiteral("/missing/report.zip")});
    EXPECT_EQ(result.exitCode, 3);
    const auto all = envelopes(result.out);
    ASSERT_EQ(all.size(), 1U);
    EXPECT_EQ(all[0].value(QStringLiteral("error")).toObject().value(QStringLiteral("code")),
              QStringLiteral("diag.report_write_failed"));
}

#ifndef _WIN32
TEST_F(DiagCli, CtrlCEndsTheCheckWith130) {
    useScenario(R"({"probes": [{"serialNumber": 801000001, "connectDelayMs": 4000}]})");
    QProcess process;
    process.setProcessEnvironment(fakeEnvironment());
    process.start(QStringLiteral(HIO_CLI_EXECUTABLE),
                  {QStringLiteral("chester"), QStringLiteral("app"), QStringLiteral("check")});
    ASSERT_TRUE(process.waitForStarted(10000));
    QByteArray out;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!out.contains("Device power") && std::chrono::steady_clock::now() < deadline) {
        process.waitForReadyRead(100);
        out += process.readAllStandardOutput();
    }
    ASSERT_TRUE(out.contains("Device power")) << out.toStdString();
    ASSERT_EQ(::kill(static_cast<int>(process.processId()), SIGINT), 0);
    ASSERT_TRUE(process.waitForFinished(20000));
    EXPECT_EQ(process.exitCode(), 130);
}
#endif

} // namespace
