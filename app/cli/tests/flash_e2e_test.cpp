// End-to-end tests of the M2 commands (plan/features/0003, phase F5): the real `hio` executable
// with the fake J-Link library and its flash model, the fake nRF Util and a local HTTP catalog.

#include "cli_process.h"
#include "hex_builder.h"
#include "http_fixture.h"
#include "zip_builder.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
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

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#ifndef _WIN32
#include <QtContainerFwd>

#include <csignal>
#include <cstddef>
#else
#include <QDir>
#include <QLibraryInfo>
#endif

namespace {

using hio::cli::test::CliProcess;
using hio::cli::test::RunResult;
using hio::firmware::test::HttpFixture;

// The PIB of py-hardwario's test vector at UICR + 0x80 (0x10001080 = 268439680).
constexpr const char* pibHex =
    "babecafe027b48415244574152494f0000000000000000434845535445522d4d00000000000000004344474c53"
    "00000000000052332e3200000032313539303138323437003031323334353637383961626364656630313233343536"
    "373839616263646566003132333435360000000000000000000000a8d83379ffffffffff";

std::vector<QJsonObject> envelopes(const std::string& out) {
    std::vector<QJsonObject> result;
    for (const QByteArray& line : QByteArray::fromStdString(out).split('\n')) {
        if (!line.isEmpty()) {
            result.push_back(QJsonDocument::fromJson(line).object());
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

std::string appHex() {
    return hio::firmware::test::hexRecord(0, 0x2000, {1, 2, 3, 4, 5, 6, 7, 8}) +
           hio::firmware::test::eofRecord();
}

class FlashCli : public CliProcess {
protected:
    void SetUp() override {
        useScenario(QStringLiteral(R"({"probes": [
            {"serialNumber": 801000001, "product": "Test Probe",
             "chip": {"memory": [{"address": 268439680, "hex": "%1"}]}},
            {"serialNumber": 801000002, "product": "Test Probe 2", "chip": {"family": "nrf91"}}]})")
                        .arg(QString::fromLatin1(pibHex)));
        writeFile(QStringLiteral("app.hex"), QByteArray::fromStdString(appHex()));
    }

    void useScenario(const QString& json) const {
        writeFile(QStringLiteral("scenario.json"), json.toUtf8());
    }

    void writeFile(const QString& name, const QByteArray& bytes) const {
        ASSERT_TRUE(hio::firmware::test::writeFile(path(name), bytes));
    }
    [[nodiscard]] QString path(const QString& name) const {
        return home() + QLatin1Char('/') + name;
    }

    [[nodiscard]] QProcessEnvironment fakeEnvironment() const {
        QProcessEnvironment env = environment();
        env.insert(QStringLiteral("HIO_FAKE_JLINK_SCENARIO"),
                   path(QStringLiteral("scenario.json")));
        env.insert(QStringLiteral("HIO_JLINK_LIBRARY"), QStringLiteral(HIO_FAKE_JLINK_LIBRARY));
        env.insert(QStringLiteral("HIO_FAKE_NRFUTIL_STEP_MS"), QStringLiteral("5"));
        env.remove(QStringLiteral("HIO_NRFUTIL"));
        // No nrfutil on PATH. On Windows hio.exe finds the Qt DLLs through PATH, so it keeps Qt's
        // bin directory (which has no nrfutil).
#ifdef _WIN32
        env.insert(QStringLiteral("PATH"),
                   QDir::toNativeSeparators(QLibraryInfo::path(QLibraryInfo::BinariesPath)));
#else
        env.insert(QStringLiteral("PATH"), QStringLiteral("/nonexistent-path-for-tests"));
#endif
        return env;
    }

    [[nodiscard]] RunResult hio(const QStringList& args) const {
        return run(args, fakeEnvironment());
    }

    // Runs the child while this thread's event loop keeps running (the HTTP fixture lives here).
    [[nodiscard]] static RunResult runWithEvents(const QStringList& args,
                                                 const QProcessEnvironment& env) {
        QProcess process;
        process.setProcessEnvironment(env);
        process.start(QStringLiteral(HIO_CLI_EXECUTABLE), args);
        RunResult result;
        if (!process.waitForStarted(10000)) {
            return result;
        }
        process.closeWriteChannel();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (process.state() != QProcess::NotRunning &&
               std::chrono::steady_clock::now() < deadline) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            process.waitForFinished(5);
        }
        result.normalExit = process.exitStatus() == QProcess::NormalExit;
        result.exitCode = process.state() == QProcess::NotRunning ? process.exitCode() : -1;
        result.out = process.readAllStandardOutput().toStdString();
        result.err = process.readAllStandardError().toStdString();
        normalizeLineEnds(result);
        return result;
    }
};

TEST_F(FlashCli, FlashFileJsonStream) {
    const RunResult result =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("flash"), path(QStringLiteral("app.hex"))});
    ASSERT_EQ(result.exitCode, 0) << result.out << result.err;
    const auto all = envelopes(result.out);
    ASSERT_GE(all.size(), 5U);
    QStringList phases;
    for (std::size_t i = 0; i + 1 < all.size(); ++i) {
        const QJsonObject event = data(all[i]);
        EXPECT_EQ(event.value(QStringLiteral("type")).toString(), QStringLiteral("progress"));
        const QString phase = event.value(QStringLiteral("phase")).toString();
        if (phases.isEmpty() || phases.back() != phase) {
            phases.append(phase);
        }
    }
    EXPECT_EQ(phases, (QStringList{QStringLiteral("connecting"), QStringLiteral("erasing"),
                                   QStringLiteral("programming"), QStringLiteral("verifying"),
                                   QStringLiteral("resetting")}));
    const QJsonObject final = data(all.back());
    EXPECT_EQ(final.value(QStringLiteral("type")).toString(), QStringLiteral("result"));
    EXPECT_EQ(final.value(QStringLiteral("target")).toString(), QStringLiteral("app"));
    EXPECT_EQ(final.value(QStringLiteral("part")).toString(), QStringLiteral("nRF52840"));
    EXPECT_FALSE(final.value(QStringLiteral("halted")).toBool());
    const QJsonObject image = final.value(QStringLiteral("image")).toObject();
    EXPECT_EQ(image.value(QStringLiteral("source")).toString(), QStringLiteral("file"));
    EXPECT_EQ(image.value(QStringLiteral("bytes")).toInt(), 8);
    EXPECT_EQ(image.value(QStringLiteral("sha256")).toString().size(), 64);
}

TEST_F(FlashCli, FlashHumanOutput) {
    const RunResult result = hio({QStringLiteral("chester"), QStringLiteral("app"),
                                  QStringLiteral("flash"), path(QStringLiteral("app.hex"))});
    ASSERT_EQ(result.exitCode, 0) << result.err;
    EXPECT_EQ(result.out, "Successfully completed\n");
    EXPECT_NE(result.err.find("Using J-Link 801000001 (Test Probe)"), std::string::npos)
        << result.err;
    EXPECT_NE(result.err.find("Programming..."), std::string::npos);
    EXPECT_NE(result.err.find("Target: nRF52840 (app)"), std::string::npos);
}

TEST_F(FlashCli, SubcommandSerialNumberWinsOverTheGroup) {
    // Probe 2 has an nRF91: flashing the app there is a wrong family (exit 6).
    const RunResult group = hio({QStringLiteral("chester"), QStringLiteral("app"),
                                 QStringLiteral("-n"), QStringLiteral("801000002"),
                                 QStringLiteral("flash"), path(QStringLiteral("app.hex"))});
    EXPECT_EQ(group.exitCode, 6) << group.err;
    EXPECT_NE(group.err.find("Expected the application MCU (nRF52840) but found nRF9151."),
              std::string::npos);
    const RunResult own =
        hio({QStringLiteral("chester"), QStringLiteral("app"), QStringLiteral("-n"),
             QStringLiteral("801000002"), QStringLiteral("flash"), QStringLiteral("-n"),
             QStringLiteral("801000001"), path(QStringLiteral("app.hex"))});
    EXPECT_EQ(own.exitCode, 0) << own.err;
}

TEST_F(FlashCli, InputErrors) {
    const RunResult missing =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("flash"), path(QStringLiteral("missing.hex"))});
    EXPECT_EQ(missing.exitCode, 2);
    EXPECT_EQ(errorCode(missing), QStringLiteral("usage.invalid_argument"));

    writeFile(QStringLiteral("bad.hex"), ":0100000001FF\n");
    const RunResult bad =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("flash"), path(QStringLiteral("bad.hex"))});
    EXPECT_EQ(bad.exitCode, 7);
    EXPECT_EQ(errorCode(bad), QStringLiteral("firmware.hex_parse_error"));

    // No argument and no build output in the working directory.
    const RunResult none =
        hio({QStringLiteral("chester"), QStringLiteral("app"), QStringLiteral("flash")});
    EXPECT_EQ(none.exitCode, 2);
    EXPECT_NE(none.err.find("no build output was found"), std::string::npos) << none.err;
}

TEST_F(FlashCli, FlashByCatalogId) {
#ifndef HIO_TEST_OVERRIDES
    GTEST_SKIP() << "needs HIO_TEST_OVERRIDES (HIO_CATALOG_URL)";
#endif
    HttpFixture http;
    ASSERT_TRUE(http.listening());
    const QString id = QStringLiteral("0123456789abcdef0123456789abcdef");
    http.set(QStringLiteral("/chester/%1/hex").arg(id),
             HttpFixture::Response::ok(QByteArray::fromStdString(appHex())));
    QProcessEnvironment env = fakeEnvironment();
    env.insert(QStringLiteral("HIO_CATALOG_URL"), http.baseUrl());
    const QStringList args{QStringLiteral("--json"), QStringLiteral("chester"),
                           QStringLiteral("app"), QStringLiteral("flash"), id};
    const RunResult first = runWithEvents(args, env);
    ASSERT_EQ(first.exitCode, 0) << first.out << first.err;
    const QJsonObject image =
        data(envelopes(first.out).back()).value(QStringLiteral("image")).toObject();
    EXPECT_EQ(image.value(QStringLiteral("source")).toString(), QStringLiteral("catalog"));
    EXPECT_EQ(image.value(QStringLiteral("id")).toString(), id);
    EXPECT_FALSE(image.value(QStringLiteral("cached")).toBool());
    const auto events = envelopes(first.out);
    EXPECT_EQ(data(events.front()).value(QStringLiteral("phase")).toString(),
              QStringLiteral("downloading"));
    // The second run uses the cache.
    const RunResult second = runWithEvents(args, env);
    ASSERT_EQ(second.exitCode, 0) << second.err;
    EXPECT_TRUE(data(envelopes(second.out).back())
                    .value(QStringLiteral("image"))
                    .toObject()
                    .value(QStringLiteral("cached"))
                    .toBool());
    EXPECT_EQ(http.requests(QStringLiteral("/chester/%1/hex").arg(id)), 1);

    // 404 -> firmware.not_found_in_catalog (exit 7).
    const RunResult notFound =
        runWithEvents({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
                       QStringLiteral("flash"), QStringLiteral("ffffffffffffffffffffffffffffffff")},
                      env);
    EXPECT_EQ(notFound.exitCode, 7);
    EXPECT_EQ(errorCode(notFound), QStringLiteral("firmware.not_found_in_catalog"));
}

TEST_F(FlashCli, CatalogNetworkDown) {
#ifndef HIO_TEST_OVERRIDES
    GTEST_SKIP() << "needs HIO_TEST_OVERRIDES (HIO_CATALOG_URL)";
#endif
    QProcessEnvironment env = fakeEnvironment();
    env.insert(QStringLiteral("HIO_CATALOG_URL"), QStringLiteral("http://127.0.0.1:1/chester"));
    const RunResult result =
        run({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("flash"), QStringLiteral("0123456789abcdef0123456789abcdef")},
            env);
    EXPECT_EQ(result.exitCode, 8) << result.out;
    EXPECT_EQ(errorCode(result), QStringLiteral("network.download_failed"));
}

TEST_F(FlashCli, EraseAllNeedsYesWithoutATerminal) {
    const RunResult refused = hio({QStringLiteral("chester"), QStringLiteral("app"),
                                   QStringLiteral("erase"), QStringLiteral("--all")});
    EXPECT_EQ(refused.exitCode, 2);
    EXPECT_NE(refused.err.find("This operation needs confirmation."), std::string::npos);
    const RunResult lteRefused = hio({QStringLiteral("--json"), QStringLiteral("chester"),
                                      QStringLiteral("lte"), QStringLiteral("erase")});
    EXPECT_EQ(lteRefused.exitCode, 2);
    EXPECT_EQ(errorCode(lteRefused), QStringLiteral("usage.confirmation_required"));
    const RunResult recoverRefused =
        hio({QStringLiteral("chester"), QStringLiteral("app"), QStringLiteral("recover")});
    EXPECT_EQ(recoverRefused.exitCode, 2);
    // Code-only erase never asks; --all with --yes runs.
    EXPECT_EQ(
        hio({QStringLiteral("chester"), QStringLiteral("app"), QStringLiteral("erase")}).exitCode,
        0);
    const RunResult all =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("erase"), QStringLiteral("--all"), QStringLiteral("--yes")});
    EXPECT_EQ(all.exitCode, 0) << all.out;
    EXPECT_EQ(data(envelopes(all.out).back()).value(QStringLiteral("scope")).toString(),
              QStringLiteral("all"));
}

TEST_F(FlashCli, ResetAndPibAndUicr) {
    const RunResult reset =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("reset"), QStringLiteral("--halt")});
    ASSERT_EQ(reset.exitCode, 0) << reset.out;
    ASSERT_EQ(envelopes(reset.out).size(), 1U); // one result envelope
    EXPECT_TRUE(data(envelopes(reset.out).front()).value(QStringLiteral("halted")).toBool());

    const RunResult pib =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("pib"), QStringLiteral("read")});
    ASSERT_EQ(pib.exitCode, 0) << pib.out << pib.err;
    ASSERT_EQ(envelopes(pib.out).size(), 1U);
    const QJsonObject fields = data(envelopes(pib.out).front());
    EXPECT_EQ(fields.value(QStringLiteral("signature")).toString(), QStringLiteral("0xbabecafe"));
    EXPECT_EQ(fields.value(QStringLiteral("version")).toString(), QStringLiteral("0x02"));
    EXPECT_EQ(fields.value(QStringLiteral("size")).toString(), QStringLiteral("0x7b"));
    EXPECT_EQ(fields.value(QStringLiteral("crc")).toString(), QStringLiteral("0xa8d83379"));
    EXPECT_EQ(fields.value(QStringLiteral("serial_number")).toString(),
              QStringLiteral("2159018247"));
    EXPECT_EQ(fields.value(QStringLiteral("claim_token")).toString(),
              QStringLiteral("0123456789abcdef0123456789abcdef"));

    const RunResult human = hio({QStringLiteral("chester"), QStringLiteral("app"),
                                 QStringLiteral("pib"), QStringLiteral("read")});
    EXPECT_EQ(human.out,
              "Vendor name: HARDWARIO\nProduct name: CHESTER-M\nHardware variant: CDGLS\n"
              "Hardware revision: R3.2\nSerial number: 2159018247\n"
              "Claim token: 0123456789abcdef0123456789abcdef\nBLE passkey: 123456\n");

    const RunResult hex =
        hio({QStringLiteral("chester"), QStringLiteral("app"), QStringLiteral("uicr"),
             QStringLiteral("read"), QStringLiteral("--format"), QStringLiteral("hex"),
             QStringLiteral("-")});
    EXPECT_EQ(hex.exitCode, 0);
    EXPECT_EQ(hex.out, pibHex); // lowercase hex, no newline (py-hardwario)
    const RunResult bin =
        hio({QStringLiteral("chester"), QStringLiteral("app"), QStringLiteral("uicr"),
             QStringLiteral("read"), QStringLiteral("--format"), QStringLiteral("bin"),
             path(QStringLiteral("uicr.bin"))});
    EXPECT_EQ(bin.exitCode, 0);
    QFile file(path(QStringLiteral("uicr.bin")));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll().toHex(), QByteArray(pibHex));

    // An erased PIB is pib.invalid (exit 6).
    ASSERT_EQ(hio({QStringLiteral("chester"), QStringLiteral("app"), QStringLiteral("erase"),
                   QStringLiteral("--all"), QStringLiteral("--yes")})
                  .exitCode,
              0);
    // (the fake's memory is per process: a new run sees the scenario's PIB again)
    useScenario(QStringLiteral(R"({"probes": [{"serialNumber": 801000001}]})"));
    const RunResult invalid =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
             QStringLiteral("pib"), QStringLiteral("read")});
    EXPECT_EQ(invalid.exitCode, 6);
    EXPECT_EQ(errorCode(invalid), QStringLiteral("pib.invalid"));
}

TEST_F(FlashCli, LteBundleWithTheFakeNrfUtil) {
    const QByteArray modem = hio::firmware::test::zipBytes(
        {{.name = "firmware.update.image.segments.0.hex", .data = appHex()}});
    writeFile(QStringLiteral("bundle.zip"),
              hio::firmware::test::zipBytes({{.name = "modem.zip", .data = modem.toStdString()},
                                             {.name = "application.hex", .data = appHex()}}));
    useScenario(QStringLiteral(
        R"({"probes": [{"serialNumber": 801000002, "chip": {"family": "nrf91"}}]})"));
    QProcessEnvironment env = fakeEnvironment();
    env.insert(QStringLiteral("HIO_FAKE_NRFUTIL_LOG"), path(QStringLiteral("nrfutil.log")));
    const RunResult result =
        run({QStringLiteral("--json"), QStringLiteral("--nrfutil"),
             QStringLiteral(HIO_FAKE_NRFUTIL), QStringLiteral("chester"), QStringLiteral("lte"),
             QStringLiteral("flash"), path(QStringLiteral("bundle.zip"))},
            env);
    ASSERT_EQ(result.exitCode, 0) << result.out << result.err;
    const auto all = envelopes(result.out);
    EXPECT_EQ(data(all.front()).value(QStringLiteral("phase")).toString(), QStringLiteral("modem"));
    EXPECT_EQ(data(all.front()).value(QStringLiteral("step")).toInt(), 1);
    EXPECT_EQ(data(all.front()).value(QStringLiteral("steps")).toInt(), 2);
    const QJsonObject final = data(all.back());
    EXPECT_EQ(final.value(QStringLiteral("operation")).toString(), QStringLiteral("lteBundle"));
    EXPECT_EQ(final.value(QStringLiteral("part")).toString(), QStringLiteral("nRF9151"));
    QFile log(path(QStringLiteral("nrfutil.log")));
    ASSERT_TRUE(log.open(QIODevice::ReadOnly));
    EXPECT_TRUE(log.readAll().contains("device program --firmware"));

    // A zip that is neither a bundle nor modem firmware: exit 7, nothing programmed.
    writeFile(QStringLiteral("other.zip"),
              hio::firmware::test::zipBytes({{.name = "readme.txt", .data = "x"}}));
    const RunResult other =
        run({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("lte"),
             QStringLiteral("flash"), path(QStringLiteral("other.zip"))},
            env);
    EXPECT_EQ(other.exitCode, 7);
    EXPECT_EQ(errorCode(other), QStringLiteral("firmware.invalid_image"));
}

TEST_F(FlashCli, NrfUtilMissing) {
    writeFile(QStringLiteral("mfw.zip"),
              hio::firmware::test::zipBytes(
                  {{.name = "firmware.update.image.segments.0.hex", .data = appHex()}}));
    const RunResult result =
        hio({QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("lte"),
             QStringLiteral("flash"), path(QStringLiteral("mfw.zip"))});
    EXPECT_EQ(result.exitCode, 4) << result.out;
    EXPECT_EQ(errorCode(result), QStringLiteral("dependency.nrfutil_not_found"));
}

#ifndef _WIN32
TEST_F(FlashCli, CtrlCDuringProgrammingCancelsAndReleasesTheProbe) {
    useScenario(QStringLiteral(R"({"probes": [{"serialNumber": 801000001,
        "chip": {"readDelayMs": 200}}]})"));
    // 64 KiB: four verify chunks of 200 ms each.
    std::string text;
    for (std::uint32_t address = 0; address < 64 * 1024; address += 16) {
        if (address % 0x10000 == 0) {
            text += hio::firmware::test::linearAddress(static_cast<std::uint16_t>(address >> 16U));
        }
        text += hio::firmware::test::hexRecord(0, static_cast<std::uint16_t>(address & 0xFFFFU),
                                               std::vector<std::uint8_t>(16, 0x11));
    }
    writeFile(QStringLiteral("big.hex"),
              QByteArray::fromStdString(text + hio::firmware::test::eofRecord()));
    QProcess process;
    process.setProcessEnvironment(fakeEnvironment());
    process.start(QStringLiteral(HIO_CLI_EXECUTABLE),
                  {QStringLiteral("--json"), QStringLiteral("chester"), QStringLiteral("app"),
                   QStringLiteral("flash"), path(QStringLiteral("big.hex"))});
    ASSERT_TRUE(process.waitForStarted(10000));
    QByteArray out;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!out.contains("\"verifying\"") && std::chrono::steady_clock::now() < deadline) {
        process.waitForReadyRead(50);
        out += process.readAllStandardOutput();
    }
    ASSERT_TRUE(out.contains("\"verifying\"")) << out.toStdString();
    ASSERT_EQ(::kill(static_cast<int>(process.processId()), SIGINT), 0);
    ASSERT_TRUE(process.waitForFinished(30000));
    out += process.readAllStandardOutput();
    EXPECT_EQ(process.exitCode(), 130) << out.toStdString();
    const auto all = envelopes(out.toStdString());
    EXPECT_EQ(all.back()
                  .value(QStringLiteral("error"))
                  .toObject()
                  .value(QStringLiteral("code"))
                  .toString(),
              QStringLiteral("cancelled"));
    // The probe is free again: `jlink list` shows nobody using it.
    const RunResult list =
        hio({QStringLiteral("--json"), QStringLiteral("jlink"), QStringLiteral("list")});
    ASSERT_EQ(list.exitCode, 0);
    const QJsonArray probes =
        data(envelopes(list.out).front()).value(QStringLiteral("probes")).toArray();
    ASSERT_EQ(probes.size(), 1);
    EXPECT_TRUE(probes.at(0).toObject().value(QStringLiteral("inUseBy")).isNull());
}
#endif

} // namespace
