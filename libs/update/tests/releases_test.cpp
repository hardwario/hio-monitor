// pickLatest on a trimmed GitHub release list (plan/features/0016 A1).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/update/releases.h"
#include "hio/update/semver.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QUrl>
#include <QtCore/qnamespace.h>
#include <QtTypes>

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>

namespace {

using hio::update::pickLatest;
using hio::update::ReleaseInfo;
using hio::update::SemVer;

QByteArray fixture() {
    QFile file(QStringLiteral(HIO_UPDATE_TEST_DATA "/releases.json"));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

SemVer v(const char* text) { return SemVer::parse(text).value_or(SemVer{}); }

// The release of a pickLatest() result (an empty ReleaseInfo when there is none).
hio::update::ReleaseInfo releaseOf(const hio::Result<std::optional<ReleaseInfo>>& result) {
    return result.value_or(std::nullopt).value_or(ReleaseInfo{});
}

TEST(PickLatest, StableIgnoresV1DraftsPrereleasesAndBadTags) {
    const auto latest = pickLatest(fixture(), false);
    ASSERT_TRUE(latest.has_value());
    ASSERT_TRUE(latest->has_value());
    const ReleaseInfo release = releaseOf(latest);
    EXPECT_EQ(release.version, v("2.1.0"));
    EXPECT_EQ(release.tag, QStringLiteral("v2.1.0"));
    EXPECT_EQ(release.htmlUrl,
              QUrl(QStringLiteral("https://github.com/hardwario/hio-monitor/releases/tag/v2.1.0")));
    EXPECT_EQ(release.publishedAt,
              QDateTime::fromString(QStringLiteral("2026-12-01T10:00:00Z"), Qt::ISODate));
    EXPECT_FALSE(release.prerelease);
}

TEST(PickLatest, PrereleaseOptIn) {
    const auto latest = pickLatest(fixture(), true);
    ASSERT_TRUE(latest.has_value() && latest->has_value());
    EXPECT_EQ(releaseOf(latest).version, v("2.2.0-rc.1"));
    EXPECT_TRUE(releaseOf(latest).prerelease);
}

TEST(PickLatest, PrereleaseTagWithoutGitHubFlagIsAPrerelease) {
    const QByteArray json = R"([{"tag_name":"v2.5.0-beta.1","draft":false,"prerelease":false,
        "html_url":"https://github.com/hardwario/hio-monitor/releases/tag/v2.5.0-beta.1"},
        {"tag_name":"v2.4.0","draft":false,"prerelease":false}])";
    const auto stable = pickLatest(json, false);
    ASSERT_TRUE(stable.has_value() && stable->has_value());
    EXPECT_EQ(releaseOf(stable).version, v("2.4.0"));
    // Missing html_url: the release page by tag.
    EXPECT_EQ(releaseOf(stable).htmlUrl,
              QUrl(QStringLiteral("https://github.com/hardwario/hio-monitor/releases/tag/v2.4.0")));
    EXPECT_FALSE(releaseOf(stable).publishedAt.isValid());
    const auto pre = pickLatest(json, true);
    ASSERT_TRUE(pre.has_value() && pre->has_value());
    EXPECT_EQ(releaseOf(pre).version, v("2.5.0-beta.1"));
}

TEST(PickLatest, OnlyV2Tags) {
    const QByteArray json = R"([{"tag_name":"v1.99.0","draft":false,"prerelease":false},
        {"tag_name":"2.9.0","draft":false,"prerelease":false},
        {"tag_name":"v20.0.0","draft":false,"prerelease":false},
        {"tag_name":"v3.0.0","draft":false,"prerelease":false}])";
    const auto latest = pickLatest(json, true);
    ASSERT_TRUE(latest.has_value());
    EXPECT_FALSE(latest->has_value());
}

TEST(PickLatest, EmptyList) {
    const auto latest = pickLatest("[]", false);
    ASSERT_TRUE(latest.has_value());
    EXPECT_FALSE(latest->has_value());
}

TEST(PickLatest, MalformedJsonIsABadResponse) {
    for (const char* json : {"", "{", R"({"message":"Not Found"})", "[1,2]", "null"}) {
        const auto latest = pickLatest(QByteArray(json), false);
        ASSERT_FALSE(latest.has_value()) << json;
        EXPECT_EQ(latest.error().code, hio::ErrorCode::UpdateBadResponse) << json;
        EXPECT_FALSE(latest.error().detail.empty());
    }
}

TEST(PickLatest, SizeBound) {
    QByteArray json = "[";
    json += QByteArray(static_cast<qsizetype>(hio::update::maxResponseBytes), ' ');
    json += "]";
    const auto latest = pickLatest(json, false);
    ASSERT_FALSE(latest.has_value());
    EXPECT_EQ(latest.error().code, hio::ErrorCode::UpdateTooLarge);
    // The bad_response texts (plan 0016).
    EXPECT_EQ(latest.error().message, "The update information from GitHub could not be read.");
}

TEST(PickLatest, AtMostThirtyReleasesAreRead) {
    QByteArray json = "[";
    for (std::size_t i = 0; i < hio::update::maxReleases; ++i) {
        json += R"({"tag_name":"v2.0.)" + QByteArray::number(static_cast<qulonglong>(i)) +
                R"(","draft":false,"prerelease":false},)";
    }
    json += R"({"tag_name":"v2.9.0","draft":false,"prerelease":false}])";
    const auto latest = pickLatest(json, false);
    ASSERT_TRUE(latest.has_value() && latest->has_value());
    EXPECT_EQ(releaseOf(latest).version, v("2.0.29"));
}

TEST(CheckResult, NewerOnlyWhenHigher) {
    const auto latest = pickLatest(fixture(), false);
    ASSERT_TRUE(latest.has_value());
    const auto older = hio::update::makeCheckResult(v("2.0.3"), *latest);
    ASSERT_TRUE(older.newer.has_value());
    EXPECT_EQ(older.newer.value_or(ReleaseInfo{}).version, v("2.1.0"));
    const auto same = hio::update::makeCheckResult(v("2.1.0"), *latest);
    EXPECT_FALSE(same.newer.has_value());
    ASSERT_TRUE(same.latest.has_value());
    // A development build after 2.1.0 (2.1.1-dev.3) is not offered 2.1.0; one before (2.1.0-dev.3,
    // numeric part 2.1.0) is not offered it either.
    EXPECT_FALSE(hio::update::makeCheckResult(v("2.1.1-dev.3+abc"), *latest).newer.has_value());
    EXPECT_FALSE(hio::update::makeCheckResult(v("2.1.0-dev.3+abc"), *latest).newer.has_value());
    // A release candidate of 2.1.0 is offered 2.1.0.
    EXPECT_TRUE(hio::update::makeCheckResult(v("2.1.0-rc.2"), *latest).newer.has_value());
    EXPECT_FALSE(hio::update::makeCheckResult(v("2.0.0"), std::nullopt).newer.has_value());
}

} // namespace
