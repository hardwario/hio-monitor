// End-to-end tests of `hio update check` (plan/features/0016 B1): the real `hio` executable against
// the local HTTP fixture through the test-only HIO_UPDATE_URL override (HIO_TEST_OVERRIDES builds).

#include "cli_process.h"
#include "http_fixture.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QtContainerFwd>

#include <gtest/gtest.h>

#include <chrono>
#include <string>

namespace {

using hio::cli::test::CliProcess;
using hio::cli::test::RunResult;
using hio::firmware::test::HttpFixture;

const QString releasesPath = QStringLiteral("/repos/hardwario/hio-monitor/releases");

// A v2 release far ahead of any build of this source tree, a v1 release and a pre-release above it.
const QByteArray releases = R"([
  {"tag_name":"v2.999.0-rc.1","draft":false,"prerelease":true,
   "html_url":"https://github.com/hardwario/hio-monitor/releases/tag/v2.999.0-rc.1",
   "published_at":"2027-02-01T10:00:00Z"},
  {"tag_name":"v2.998.0","draft":false,"prerelease":false,
   "html_url":"https://github.com/hardwario/hio-monitor/releases/tag/v2.998.0",
   "published_at":"2027-01-01T10:00:00Z"},
  {"tag_name":"v1.9.0","draft":false,"prerelease":false,
   "html_url":"https://github.com/hardwario/hio-monitor/releases/tag/v1.9.0",
   "published_at":"2026-01-01T10:00:00Z"}
])";

QJsonObject lastEnvelope(const std::string& out) {
    QJsonObject last;
    for (const QByteArray& line : QByteArray::fromStdString(out).split('\n')) {
        if (!line.isEmpty()) {
            last = QJsonDocument::fromJson(line).object();
        }
    }
    return last;
}

class UpdateCli : public CliProcess {
protected:
    void SetUp() override {
#ifndef HIO_TEST_OVERRIDES
        GTEST_SKIP() << "needs HIO_TEST_OVERRIDES (HIO_UPDATE_URL)";
#endif
        ASSERT_TRUE(m_http.listening());
    }

    [[nodiscard]] QProcessEnvironment updateEnvironment() const {
        QProcessEnvironment env = environment();
        env.insert(QStringLiteral("HIO_UPDATE_URL"), m_http.origin() + releasesPath);
        return env;
    }

    // Runs the child while this thread's event loop keeps running (the HTTP fixture lives here).
    [[nodiscard]] RunResult runWithEvents(const QStringList& args) const {
        QProcess process;
        process.setProcessEnvironment(updateEnvironment());
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
        return result;
    }

    [[nodiscard]] HttpFixture& http() { return m_http; }

private:
    HttpFixture m_http;
};

TEST_F(UpdateCli, NewerVersionHumanAndJson) {
    http().set(releasesPath, HttpFixture::Response::ok(releases));
    const RunResult human = runWithEvents({QStringLiteral("update"), QStringLiteral("check")});
    ASSERT_EQ(human.exitCode, 0) << human.out << human.err;
    EXPECT_NE(human.out.find("a newer version is available: 2.998.0"), std::string::npos)
        << human.out;
    EXPECT_NE(human.out.find("Download: https://github.com/hardwario/hio-monitor/releases/tag/"
                             "v2.998.0"),
              std::string::npos)
        << human.out;

    const RunResult json = runWithEvents(
        {QStringLiteral("update"), QStringLiteral("check"), QStringLiteral("--json")});
    ASSERT_EQ(json.exitCode, 0) << json.out << json.err;
    const QJsonObject envelope = lastEnvelope(json.out);
    EXPECT_TRUE(envelope.value(QStringLiteral("ok")).toBool()) << json.out;
    const QJsonObject data = envelope.value(QStringLiteral("data")).toObject();
    EXPECT_EQ(data.value(QStringLiteral("latest")).toString(), QStringLiteral("2.998.0"));
    EXPECT_TRUE(data.value(QStringLiteral("update_available")).toBool());
    EXPECT_FALSE(data.value(QStringLiteral("prerelease")).toBool());
    EXPECT_EQ(data.value(QStringLiteral("url")).toString(),
              QStringLiteral("https://github.com/hardwario/hio-monitor/releases/tag/v2.998.0"));
    EXPECT_EQ(data.value(QStringLiteral("published_at")).toString(),
              QStringLiteral("2027-01-01T10:00:00Z"));
    EXPECT_FALSE(data.value(QStringLiteral("current")).toString().isEmpty());
    EXPECT_EQ(http().requests(releasesPath), 2);
}

TEST_F(UpdateCli, PrereleaseOptIn) {
    http().set(releasesPath, HttpFixture::Response::ok(releases));
    const RunResult json = runWithEvents({QStringLiteral("--json"), QStringLiteral("update"),
                                          QStringLiteral("check"), QStringLiteral("--prerelease")});
    ASSERT_EQ(json.exitCode, 0) << json.out << json.err;
    const QJsonObject data = lastEnvelope(json.out).value(QStringLiteral("data")).toObject();
    EXPECT_EQ(data.value(QStringLiteral("latest")).toString(), QStringLiteral("2.999.0-rc.1"));
    EXPECT_TRUE(data.value(QStringLiteral("prerelease")).toBool());
}

TEST_F(UpdateCli, NoV2ReleaseIsUpToDate) {
    http().set(releasesPath, HttpFixture::Response::ok(
                                 R"([{"tag_name":"v1.9.0","draft":false,"prerelease":false}])"));
    const RunResult human = runWithEvents({QStringLiteral("update"), QStringLiteral("check")});
    ASSERT_EQ(human.exitCode, 0) << human.out << human.err;
    EXPECT_NE(human.out.find("you have the latest version."), std::string::npos) << human.out;
    const RunResult json = runWithEvents(
        {QStringLiteral("--json"), QStringLiteral("update"), QStringLiteral("check")});
    ASSERT_EQ(json.exitCode, 0);
    const QJsonObject data = lastEnvelope(json.out).value(QStringLiteral("data")).toObject();
    EXPECT_FALSE(data.value(QStringLiteral("update_available")).toBool());
    EXPECT_TRUE(data.value(QStringLiteral("latest")).isNull());
    EXPECT_TRUE(data.value(QStringLiteral("url")).isNull());
}

TEST_F(UpdateCli, RateLimitIsANetworkError) {
    HttpFixture::Response response = HttpFixture::Response::withStatus(403, "{}");
    response.headers = {"X-RateLimit-Remaining: 0", "X-RateLimit-Reset: 1798761600"};
    http().set(releasesPath, response);
    const RunResult json = runWithEvents(
        {QStringLiteral("--json"), QStringLiteral("update"), QStringLiteral("check")});
    EXPECT_EQ(json.exitCode, 8) << json.out;
    const QJsonObject error = lastEnvelope(json.out).value(QStringLiteral("error")).toObject();
    EXPECT_EQ(error.value(QStringLiteral("code")).toString(),
              QStringLiteral("update.rate_limited"));
    const RunResult human = runWithEvents({QStringLiteral("update"), QStringLiteral("check")});
    EXPECT_EQ(human.exitCode, 8);
    EXPECT_NE(human.err.find("GitHub temporarily refused the update check."), std::string::npos)
        << human.err;
}

TEST_F(UpdateCli, ServerDown) {
    QProcessEnvironment env = environment();
    env.insert(QStringLiteral("HIO_UPDATE_URL"), QStringLiteral("http://127.0.0.1:1/releases"));
    const RunResult json =
        run({QStringLiteral("--json"), QStringLiteral("update"), QStringLiteral("check")}, env);
    EXPECT_EQ(json.exitCode, 8) << json.out;
    EXPECT_EQ(lastEnvelope(json.out)
                  .value(QStringLiteral("error"))
                  .toObject()
                  .value(QStringLiteral("code"))
                  .toString(),
              QStringLiteral("update.network"));
}

} // namespace
