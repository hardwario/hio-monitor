// UpdateCheck against the local HTTP fixture (plan/features/0016 A2): success, rate limit,
// timeout, oversize, redirect to another origin, cancel. Left out under TSan (CMakeLists.txt).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/update/releases.h"
#include "hio/update/semver.h"
#include "hio/update/update_check.h"
#include "http_fixture.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QTimer>
#include <QUrl>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>

namespace {

using hio::firmware::test::HttpFixture;
using hio::update::ReleaseInfo;
using hio::update::SemVer;
using hio::update::UpdateCheck;

const QString releasesPath = QStringLiteral("/repos/hardwario/hio-monitor/releases");

QByteArray fixture() {
    QFile file(QStringLiteral(HIO_UPDATE_TEST_DATA "/releases.json"));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

SemVer v(const char* text) { return SemVer::parse(text).value_or(SemVer{}); }

QUrl url(const HttpFixture& server) { return {server.origin() + releasesPath}; }

hio::Result<hio::update::CheckResult> run(UpdateCheck& check, QNetworkAccessManager& network,
                                          const QUrl& target, const char* current,
                                          bool prerelease = false) {
    check.start(network, target, v(current), prerelease);
    return hio::update::waitForCheck(check);
}

TEST(UpdateCheck, FindsANewerRelease) {
    HttpFixture server;
    ASSERT_TRUE(server.listening());
    server.set(releasesPath, HttpFixture::Response::ok(fixture()));
    QNetworkAccessManager network;
    UpdateCheck check;
    int emitted = 0;
    QObject::connect(&check, &UpdateCheck::finished, [&emitted] { ++emitted; });
    const auto result = run(check, network, url(server), "2.0.3");
    ASSERT_TRUE(result.has_value()) << result.error().detail;
    const auto newer = result->newer;
    ASSERT_TRUE(newer.has_value());
    EXPECT_EQ(newer.value_or(ReleaseInfo{}).version, v("2.1.0"));
    EXPECT_EQ(result->current, v("2.0.3"));
    EXPECT_EQ(emitted, 1);
    EXPECT_EQ(server.requests(releasesPath), 1);
    EXPECT_FALSE(check.isRunning());
}

TEST(UpdateCheck, UpToDateAndPrerelease) {
    HttpFixture server;
    server.set(releasesPath, HttpFixture::Response::ok(fixture()));
    QNetworkAccessManager network;
    UpdateCheck check;
    const auto stable = run(check, network, url(server), "2.1.0");
    ASSERT_TRUE(stable.has_value());
    EXPECT_FALSE(stable->newer.has_value());
    ASSERT_TRUE(stable->latest.has_value());
    EXPECT_EQ(stable->latest.value_or(ReleaseInfo{}).version, v("2.1.0"));
    const auto pre = run(check, network, url(server), "2.1.0", true);
    ASSERT_TRUE(pre.has_value());
    ASSERT_TRUE(pre->newer.has_value());
    EXPECT_EQ(pre->newer.value_or(ReleaseInfo{}).version, v("2.2.0-rc.1"));
}

TEST(UpdateCheck, RateLimited) {
    HttpFixture server;
    HttpFixture::Response response = HttpFixture::Response::withStatus(403, "{}");
    response.headers = {"X-RateLimit-Remaining: 0", "X-RateLimit-Reset: 1798761600"};
    server.set(releasesPath, response);
    QNetworkAccessManager network;
    UpdateCheck check;
    const auto result = run(check, network, url(server), "2.0.0");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, hio::ErrorCode::UpdateRateLimited);
    EXPECT_NE(result.error().detail.find("2027-01-01T00:00:00Z"), std::string::npos)
        << result.error().detail;
}

TEST(UpdateCheck, OtherHttpErrorsAreBadResponses) {
    for (const int status : {403, 404, 500}) {
        HttpFixture server;
        server.set(releasesPath, HttpFixture::Response::withStatus(status, "oops"));
        QNetworkAccessManager network;
        UpdateCheck check;
        const auto result = run(check, network, url(server), "2.0.0");
        ASSERT_FALSE(result.has_value()) << status;
        EXPECT_EQ(result.error().code, hio::ErrorCode::UpdateBadResponse) << status;
    }
}

TEST(UpdateCheck, MalformedBody) {
    HttpFixture server;
    server.set(releasesPath, HttpFixture::Response::ok("<html>proxy</html>"));
    QNetworkAccessManager network;
    UpdateCheck check;
    const auto result = run(check, network, url(server), "2.0.0");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, hio::ErrorCode::UpdateBadResponse);
}

TEST(UpdateCheck, Timeout) {
    HttpFixture server;
    server.set(releasesPath, HttpFixture::Response::hanging());
    QNetworkAccessManager network;
    UpdateCheck check;
    check.setTransferTimeout(std::chrono::milliseconds(300));
    const auto result = run(check, network, url(server), "2.0.0");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, hio::ErrorCode::UpdateNetwork);
    EXPECT_NE(result.error().detail.find("timeout"), std::string::npos) << result.error().detail;
}

TEST(UpdateCheck, Oversize) {
    HttpFixture server;
    server.set(releasesPath, HttpFixture::Response::ok(QByteArray(
                                 static_cast<qsizetype>(hio::update::maxResponseBytes) + 1, ' ')));
    QNetworkAccessManager network;
    UpdateCheck check;
    const auto result = run(check, network, url(server), "2.0.0");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, hio::ErrorCode::UpdateTooLarge);
}

TEST(UpdateCheck, RedirectToAnotherOriginIsRefused) {
    HttpFixture other;
    other.set(releasesPath, HttpFixture::Response::ok(fixture()));
    HttpFixture server;
    server.set(releasesPath,
               HttpFixture::Response::redirect((other.origin() + releasesPath).toUtf8()));
    QNetworkAccessManager network;
    UpdateCheck check;
    const auto result = run(check, network, url(server), "2.0.0");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, hio::ErrorCode::UpdateNetwork);
    EXPECT_EQ(other.requests(releasesPath), 0);
}

TEST(UpdateCheck, RedirectWithinTheOriginIsFollowed) {
    HttpFixture server;
    server.set(releasesPath, HttpFixture::Response::redirect(
                                 (server.origin() + QStringLiteral("/moved")).toUtf8()));
    server.set(QStringLiteral("/moved"), HttpFixture::Response::ok(fixture()));
    QNetworkAccessManager network;
    UpdateCheck check;
    const auto result = run(check, network, url(server), "2.0.0");
    ASSERT_TRUE(result.has_value()) << result.error().detail;
    EXPECT_TRUE(result->newer.has_value());
}

TEST(UpdateCheck, Cancel) {
    HttpFixture server;
    server.set(releasesPath, HttpFixture::Response::hanging());
    QNetworkAccessManager network;
    UpdateCheck check;
    int emitted = 0;
    QObject::connect(&check, &UpdateCheck::finished, [&emitted] { ++emitted; });
    check.start(network, url(server), v("2.0.0"), false);
    QTimer::singleShot(100, &check, [&check] { check.cancel(); });
    const auto result = hio::update::waitForCheck(check);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, hio::ErrorCode::Cancelled);
    EXPECT_EQ(emitted, 1);
    check.cancel(); // no-op when nothing runs
    EXPECT_EQ(emitted, 1);
}

TEST(UpdateCheck, NoServer) {
    QNetworkAccessManager network;
    UpdateCheck check;
    // A port nobody listens on: a fixture that is destroyed at once.
    QUrl target;
    {
        const HttpFixture server;
        target = url(server);
    }
    const auto result = run(check, network, target, "2.0.0");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, hio::ErrorCode::UpdateNetwork);
}

} // namespace
