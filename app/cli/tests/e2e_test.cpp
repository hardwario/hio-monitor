// End-to-end tests: run the real `hwio` executable (HWIO_CLI_EXECUTABLE) with QProcess.

#include "cli_process.h"
#include "hwio/core/json.h"
#include "hwio/core/version.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QtContainerFwd>

#include <gtest/gtest.h>

#include <chrono>
#include <string>

#ifndef _WIN32
#include <csignal>
#endif

namespace {

using hwio::cli::test::CliProcess;
using hwio::cli::test::RunResult;

QJsonObject parseEnvelope(const std::string& line) {
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(line));
    EXPECT_TRUE(doc.isObject()) << line;
    return doc.object();
}

TEST_F(CliProcess, Version) {
    const RunResult result = run({QStringLiteral("--version")});
    ASSERT_TRUE(result.normalExit);
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_EQ(result.out, "hwio " + std::string(hwio::buildInfo().version) + "\n");
}

TEST_F(CliProcess, JsonVersion) {
    const RunResult result = run({QStringLiteral("--json"), QStringLiteral("--version")});
    ASSERT_TRUE(result.normalExit);
    EXPECT_EQ(result.exitCode, 0);
    ASSERT_TRUE(result.out.ends_with("\n"));
    EXPECT_EQ(result.out.find('\n'), result.out.size() - 1); // exactly one line
    const QJsonObject envelope = parseEnvelope(result.out);
    EXPECT_EQ(envelope.value(QStringLiteral("schema")).toInt(), 1);
    EXPECT_TRUE(envelope.value(QStringLiteral("ok")).toBool());
    const QJsonObject data = envelope.value(QStringLiteral("data")).toObject();
    EXPECT_EQ(data.value(QStringLiteral("name")).toString(), QStringLiteral("hwio"));
    EXPECT_EQ(data.value(QStringLiteral("version")).toString(),
              hwio::toQString(hwio::buildInfo().version));
}

TEST_F(CliProcess, HumanErrorAndExitCode) {
    const RunResult result = run({QStringLiteral("_selftest"), QStringLiteral("error"),
                                  QStringLiteral("io.file_not_found")});
    ASSERT_TRUE(result.normalExit);
    EXPECT_EQ(result.exitCode, 3);
    EXPECT_EQ(result.out, "");
    EXPECT_TRUE(result.err.starts_with("Error: The file was not found.\n")) << result.err;
    EXPECT_NE(result.err.find("Cause: "), std::string::npos);
    EXPECT_NE(result.err.find("What to do: "), std::string::npos);
}

TEST_F(CliProcess, JsonErrorEnvelopeAndCancelledExitCode) {
    const RunResult result = run({QStringLiteral("--json"), QStringLiteral("_selftest"),
                                  QStringLiteral("error"), QStringLiteral("cancelled")});
    ASSERT_TRUE(result.normalExit);
    EXPECT_EQ(result.exitCode, 130);
    const QJsonObject envelope = parseEnvelope(result.out);
    EXPECT_FALSE(envelope.value(QStringLiteral("ok")).toBool());
    const QJsonObject error = envelope.value(QStringLiteral("error")).toObject();
    EXPECT_EQ(error.value(QStringLiteral("code")).toString(), QStringLiteral("cancelled"));
    EXPECT_EQ(error.value(QStringLiteral("category")).toString(), QStringLiteral("cancelled"));
    EXPECT_EQ(error.value(QStringLiteral("message")).toString(),
              QStringLiteral("Cancelled by user."));
    EXPECT_TRUE(error.value(QStringLiteral("wizard")).isNull());
}

TEST_F(CliProcess, InternalErrorNamesTheLogFile) {
    const RunResult result = run({QStringLiteral("_selftest"), QStringLiteral("error"),
                                  QStringLiteral("internal.unexpected")});
    ASSERT_TRUE(result.normalExit);
    EXPECT_EQ(result.exitCode, 1);
    EXPECT_NE(result.err.find("Details: Log file: "), std::string::npos) << result.err;
    EXPECT_FALSE(QDir(logDir()).entryList({QStringLiteral("cli-*.log")}).isEmpty());
}

TEST_F(CliProcess, UnknownOptionIsUsageError) {
    const RunResult human = run({QStringLiteral("--bogus")});
    ASSERT_TRUE(human.normalExit);
    EXPECT_EQ(human.exitCode, 2);
    EXPECT_NE(human.err.find("Run `hwio --help`."), std::string::npos) << human.err;

    const RunResult json = run({QStringLiteral("--bogus"), QStringLiteral("--json")});
    EXPECT_EQ(json.exitCode, 2);
    const QJsonObject error = parseEnvelope(json.out).value(QStringLiteral("error")).toObject();
    EXPECT_EQ(error.value(QStringLiteral("code")).toString(),
              QStringLiteral("usage.invalid_argument"));
}

// Plan 0025 (ADR-0186): `hwio console history clear` deletes only the history files.
#ifdef __linux__
TEST_F(CliProcess, ConsoleHistoryClear) {
    const QString console = home() + QStringLiteral("/data/HARDWARIO/Monitor/console");
    ASSERT_TRUE(QDir().mkpath(console));
    for (const char* name : {"history-chester-app.txt", "history-chester-x.txt", "keep.log"}) {
        QFile file(console + QLatin1Char('/') + QString::fromLatin1(name));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("x\n");
    }
    const RunResult result =
        run({QStringLiteral("console"), QStringLiteral("history"), QStringLiteral("clear")});
    EXPECT_EQ(result.exitCode, 0) << result.err;
    EXPECT_NE(result.out.find("history of all devices was deleted"), std::string::npos)
        << result.out;
    EXPECT_FALSE(QFile::exists(console + QStringLiteral("/history-chester-app.txt")));
    EXPECT_FALSE(QFile::exists(console + QStringLiteral("/history-chester-x.txt")));
    EXPECT_TRUE(QFile::exists(console + QStringLiteral("/keep.log")));
    const RunResult json = run({QStringLiteral("--json"), QStringLiteral("console"),
                                QStringLiteral("history"), QStringLiteral("clear")});
    EXPECT_EQ(json.exitCode, 0) << json.err;
}
#endif

TEST_F(CliProcess, HelpHidesSelftest) {
    const RunResult result = run({QStringLiteral("--help")});
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.out.find("--log-level"), std::string::npos);
    EXPECT_EQ(result.out.find("_selftest"), std::string::npos);
}

TEST_F(CliProcess, JsonHelpIsAnEnvelope) {
    const RunResult result = run({QStringLiteral("--json"), QStringLiteral("--help")});
    EXPECT_EQ(result.exitCode, 0);
    const QJsonObject envelope = parseEnvelope(result.out);
    EXPECT_TRUE(envelope.value(QStringLiteral("ok")).toBool());
    const QString help =
        envelope.value(QStringLiteral("data")).toObject().value(QStringLiteral("help")).toString();
    EXPECT_TRUE(help.contains(QStringLiteral("--log-level"))) << result.out;
}

TEST_F(CliProcess, CzechLocaleStaysEnglish) {
    QProcessEnvironment env = environment();
    env.insert(QStringLiteral("LANG"), QStringLiteral("cs_CZ.UTF-8"));
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("cs_CZ.UTF-8"));
    env.insert(QStringLiteral("LANGUAGE"), QStringLiteral("cs"));
    const RunResult result = run(
        {QStringLiteral("_selftest"), QStringLiteral("error"), QStringLiteral("io.file_not_found")},
        env);
    EXPECT_EQ(result.exitCode, 3);
    EXPECT_TRUE(result.err.starts_with("Error: The file was not found.\n")) << result.err;
}

TEST_F(CliProcess, LogsNeverGoToStdout) {
    const RunResult result =
        run({QStringLiteral("--log-level"), QStringLiteral("debug"), QStringLiteral("--json"),
             QStringLiteral("_selftest"), QStringLiteral("error"), QStringLiteral("timeout")});
    EXPECT_EQ(result.exitCode, 9);
    EXPECT_EQ(result.out.find('\n'), result.out.size() - 1) << result.out;
    EXPECT_NE(result.err.find("[inf] hwio.core"), std::string::npos) << result.err;
}

#ifndef _WIN32
void cancelWaitWith(const QProcessEnvironment& env, int signal) {
    QProcess process;
    process.setProcessEnvironment(env);
    process.start(QStringLiteral(HWIO_CLI_EXECUTABLE),
                  {QStringLiteral("--log-level"), QStringLiteral("debug"),
                   QStringLiteral("_selftest"), QStringLiteral("wait")});
    ASSERT_TRUE(process.waitForStarted(10000));
    // Wait until the command is waiting (signal handlers are installed before it starts).
    QByteArray err;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!err.contains("waiting for cancellation") &&
           std::chrono::steady_clock::now() < deadline) {
        process.waitForReadyRead(100);
        err += process.readAllStandardError();
    }
    ASSERT_TRUE(err.contains("waiting for cancellation")) << err.toStdString();
    ASSERT_EQ(::kill(static_cast<int>(process.processId()), signal), 0);
    ASSERT_TRUE(process.waitForFinished(10000)) << "hwio did not stop after the signal";
    err += process.readAllStandardError();
    EXPECT_EQ(process.exitStatus(), QProcess::NormalExit);
    EXPECT_EQ(process.exitCode(), 130);
    EXPECT_TRUE(err.contains("Error: Cancelled by user.")) << err.toStdString();
}

TEST_F(CliProcess, CtrlCCancelsWait) { cancelWaitWith(environment(), SIGINT); }

TEST_F(CliProcess, SigtermCancelsWait) { cancelWaitWith(environment(), SIGTERM); }
#endif

} // namespace
