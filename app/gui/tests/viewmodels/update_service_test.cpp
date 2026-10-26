// UpdateService and UpdateViewModel (plan/features/0016 C1-C2, ADR-0116): scheduling with an
// injected clock, settings, banner rules (automatic vs manual check, skipped version, Later).
// The tests that talk to the local HTTP fixture are left out under TSan (CMakeLists.txt).

#include "gui_test_support.h"
#include "hio/update/semver.h"
#include "http_fixture.h"
#include "settings_store.h"
#include "update_service.h"
#include "viewmodels/update_view_model.h"

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QString>
#include <QTime>
#include <QTimeZone>
#include <QUrl>
#include <QVariant>
#include <QtCore/qnamespace.h>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <vector>

namespace {

using hio::firmware::test::HttpFixture;
using hio::gui::SettingsStore;
using hio::gui::UpdateService;
using hio::gui::UpdateViewModel;
using hio::gui::test::waitFor;

const QString releasesPath = QStringLiteral("/repos/hardwario/hio-monitor/releases");

const QByteArray releases = R"([
  {"tag_name":"v2.1.0","draft":false,"prerelease":false,
   "html_url":"https://github.com/hardwario/hio-monitor/releases/tag/v2.1.0",
   "published_at":"2026-12-01T10:00:00Z"},
  {"tag_name":"v2.0.3","draft":false,"prerelease":false}
])";

hio::update::SemVer v(const char* text) {
    return hio::update::SemVer::parse(text).value_or(hio::update::SemVer{});
}

class UpdateTest : public ::testing::Test {
protected:
    UpdateTest() : m_store(std::nullopt) { UpdateService::registerKeys(m_store, false); }

    // A service for the version `current` of the package kind `kind`, the clock at m_now.
    std::unique_ptr<UpdateService> makeService(const char* current = "2.0.3",
                                               const char* kind = "deb") {
        return std::make_unique<UpdateService>(
            m_store, m_network,
            UpdateService::Options{.now = [this] { return m_now; },
                                   .startDelay = std::chrono::milliseconds(0),
                                   .interval = std::chrono::hours(24),
                                   .url = QUrl(m_http.origin() + releasesPath),
                                   .packageKind = QString::fromLatin1(kind),
                                   .current = v(current)});
    }

    void serve(const QByteArray& body) {
        m_http.set(releasesPath, HttpFixture::Response::ok(body));
    }
    [[nodiscard]] int requests() const { return m_http.requests(releasesPath); }
    [[nodiscard]] SettingsStore& store() { return m_store; }
    void setNow(const QDateTime& now) { m_now = now; }
    [[nodiscard]] const QDateTime& now() const { return m_now; }

private:
    SettingsStore m_store;
    QNetworkAccessManager m_network;
    HttpFixture m_http;
    QDateTime m_now = QDateTime(QDate(2026, 12, 2), QTime(9, 14), QTimeZone::UTC);
};

TEST_F(UpdateTest, AutomaticCheckDueWithTheInjectedClock) {
    auto service = makeService();
    EXPECT_TRUE(service->automaticCheckDue()); // never checked
    EXPECT_TRUE(store().setValue(QStringLiteral("update/lastCheck"),
                                 now().addSecs(-3600LL).toString(Qt::ISODate)));
    EXPECT_FALSE(service->automaticCheckDue()); // 1 h ago
    setNow(now().addSecs(23LL * 3600));
    EXPECT_TRUE(service->automaticCheckDue()); // 24 h ago
    setNow(now().addSecs(-48LL * 3600));
    EXPECT_TRUE(service->automaticCheckDue()); // in the future: the clock changed
    setNow(now().addSecs(48LL * 3600));
    EXPECT_TRUE(store().setValue(QStringLiteral("update/autoCheck"), false));
    EXPECT_FALSE(service->automaticCheckDue()); // opted out
    EXPECT_TRUE(store().setValue(QStringLiteral("update/autoCheck"), true));
    EXPECT_TRUE(service->automaticCheckDue());
    EXPECT_FALSE(makeService("2.0.3", "dev")->automaticCheckDue()); // development build
}

TEST_F(UpdateTest, PrereleaseDefaultFollowsTheRunningVersion) {
    SettingsStore release(std::nullopt);
    UpdateService::registerKeys(release, false);
    EXPECT_FALSE(release.value(QStringLiteral("update/includePrerelease")).toBool());
    SettingsStore candidate(std::nullopt);
    UpdateService::registerKeys(candidate, true);
    EXPECT_TRUE(candidate.value(QStringLiteral("update/includePrerelease")).toBool());
    EXPECT_TRUE(release.value(QStringLiteral("update/autoCheck")).toBool());
}

TEST_F(UpdateTest, AutomaticCheckShowsTheBannerAndStoresTheResult) {
    serve(releases);
    auto service = makeService();
    UpdateViewModel vm(*service, store(), [](const QUrl&) {});
    service->start();
    ASSERT_TRUE(waitFor([&] { return vm.state() == UpdateViewModel::State::Available; }));
    EXPECT_TRUE(vm.bannerVisible());
    EXPECT_EQ(vm.latestVersion(), QStringLiteral("2.1.0"));
    EXPECT_EQ(vm.currentVersion(), QStringLiteral("2.0.3"));
    EXPECT_EQ(vm.bannerText(),
              QStringLiteral("HARDWARIO Monitor 2.1.0 is available (you have 2.0.3)."));
    EXPECT_EQ(vm.downloadUrl(),
              QUrl(QStringLiteral("https://github.com/hardwario/hio-monitor/releases/tag/v2.1.0")));
    EXPECT_EQ(store().value(QStringLiteral("update/lastCheck")).toString(),
              now().toString(Qt::ISODate));
    EXPECT_EQ(store().value(QStringLiteral("update/lastResult")).toString(),
              QStringLiteral("2.1.0"));
    EXPECT_FALSE(vm.lastChecked().isEmpty());
    EXPECT_EQ(requests(), 1);
}

TEST_F(UpdateTest, NoAutomaticCheckBeforeTheIntervalOrInDevelopmentBuilds) {
    serve(releases);
    EXPECT_TRUE(store().setValue(QStringLiteral("update/lastCheck"),
                                 now().addSecs(-3600LL).toString(Qt::ISODate)));
    auto service = makeService();
    service->start();
    auto dev = makeService("2.0.3", "dev");
    dev->start();
    hio::gui::test::spin(std::chrono::milliseconds(200));
    EXPECT_EQ(requests(), 0);
    // The manual check works in both.
    dev->checkNow();
    ASSERT_TRUE(waitFor([&] { return requests() == 1 && !dev->isChecking(); }));
}

TEST_F(UpdateTest, DownloadOpensTheReleasePage) {
    serve(releases);
    auto service = makeService();
    std::vector<QUrl> opened;
    UpdateViewModel vm(*service, store(), [&opened](const QUrl& url) { opened.push_back(url); });
    service->checkAutomatically();
    ASSERT_TRUE(waitFor([&] { return vm.state() == UpdateViewModel::State::Available; }));
    vm.download();
    ASSERT_EQ(opened.size(), 1U);
    EXPECT_EQ(opened.front(), vm.downloadUrl());
}

TEST_F(UpdateTest, SkippedVersionIsNotAnnouncedAgainButManualChecksIgnoreIt) {
    serve(releases);
    auto service = makeService();
    UpdateViewModel vm(*service, store(), [](const QUrl&) {});
    service->checkAutomatically();
    ASSERT_TRUE(waitFor([&] { return vm.bannerVisible(); }));
    vm.skip();
    EXPECT_FALSE(vm.bannerVisible());
    EXPECT_EQ(store().value(QStringLiteral("update/skippedVersion")).toString(),
              QStringLiteral("2.1.0"));
    // The next automatic check finds the same version: no banner.
    service->checkAutomatically();
    ASSERT_TRUE(waitFor([&] { return requests() == 2 && !service->isChecking(); }));
    EXPECT_FALSE(vm.bannerVisible());
    // A manual check reports it (About), still without the banner.
    vm.checkNow();
    EXPECT_EQ(vm.state(), UpdateViewModel::State::Checking);
    ASSERT_TRUE(waitFor([&] { return vm.state() == UpdateViewModel::State::Available; }));
    EXPECT_EQ(vm.statusText(), QStringLiteral("Version 2.1.0 is available."));
    EXPECT_FALSE(vm.bannerVisible());
    // A newer version than the skipped one is announced.
    serve(R"([{"tag_name":"v2.2.0","draft":false,"prerelease":false}])");
    service->checkAutomatically();
    ASSERT_TRUE(waitFor([&] { return vm.bannerVisible(); }));
    EXPECT_EQ(vm.latestVersion(), QStringLiteral("2.2.0"));
}

TEST_F(UpdateTest, LaterHidesTheBanner) {
    serve(releases);
    auto service = makeService();
    UpdateViewModel vm(*service, store(), [](const QUrl&) {});
    service->checkAutomatically();
    ASSERT_TRUE(waitFor([&] { return vm.bannerVisible(); }));
    vm.dismiss();
    EXPECT_FALSE(vm.bannerVisible());
    EXPECT_TRUE(store().value(QStringLiteral("update/skippedVersion")).toString().isEmpty());
}

TEST_F(UpdateTest, UpToDate) {
    serve(releases);
    auto service = makeService("2.1.0");
    UpdateViewModel vm(*service, store(), [](const QUrl&) {});
    vm.checkNow();
    ASSERT_TRUE(waitFor([&] { return vm.state() == UpdateViewModel::State::UpToDate; }));
    EXPECT_EQ(vm.statusText(), QStringLiteral("You have the latest version."));
    EXPECT_FALSE(vm.bannerVisible());
}

TEST_F(UpdateTest, FailuresAreShownOnlyForManualChecks) {
    auto service = makeService(); // nothing served: HTTP 404
    UpdateViewModel vm(*service, store(), [](const QUrl&) {});
    service->checkAutomatically();
    ASSERT_TRUE(waitFor([&] { return requests() == 1 && !service->isChecking(); }));
    EXPECT_EQ(vm.state(), UpdateViewModel::State::Idle);
    EXPECT_TRUE(store().value(QStringLiteral("update/lastCheck")).toString().isEmpty());
    vm.checkNow();
    ASSERT_TRUE(waitFor([&] { return vm.state() == UpdateViewModel::State::Failed; }));
    EXPECT_TRUE(vm.errorText().startsWith(
        QStringLiteral("The update information from GitHub could not be read.")))
        << vm.errorText().toStdString();
    EXPECT_EQ(vm.statusText(), vm.errorText());
}

TEST_F(UpdateTest, SettingsProperties) {
    auto service = makeService();
    UpdateViewModel vm(*service, store(), [](const QUrl&) {});
    EXPECT_TRUE(vm.autoCheck());
    EXPECT_FALSE(vm.includePrerelease());
    vm.setAutoCheck(false);
    vm.setIncludePrerelease(true);
    EXPECT_FALSE(store().value(QStringLiteral("update/autoCheck")).toBool());
    EXPECT_TRUE(store().value(QStringLiteral("update/includePrerelease")).toBool());
    EXPECT_TRUE(vm.lastChecked().isEmpty());
    EXPECT_TRUE(vm.statusText().isEmpty());
}

} // namespace
