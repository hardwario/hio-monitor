#pragma once

// The GitHub release list of hardwario/hio-monitor, filtered to v2 releases (plan/features/0016,
// ADR-0116, ADR-0125).
//
// pickLatest() is pure and bounded: it reads at most maxReleases entries of the JSON array
// returned by GET /repos/hardwario/hio-monitor/releases?per_page=30, drops drafts, tags that do
// not start with "v2." (v1 releases share the repository), tags that are not semantic versions,
// and - unless `includePrerelease` - pre-releases (GitHub's "prerelease" flag or a SemVer
// pre-release tag), and returns the highest remaining version (nullopt when none is left).

#include "hio/core/result.h"
#include "hio/update/semver.h"

#include <QByteArrayView>
#include <QDateTime>
#include <QString>
#include <QUrl>

#include <cstddef>
#include <optional>

namespace hio::update {

// Bounds of the check (plan 0016).
inline constexpr std::size_t maxResponseBytes = 1024ULL * 1024; // 1 MiB
inline constexpr std::size_t maxReleases = 30;

// The default release list (ADR-0116); the CLI and GUI use releasesUrl().
inline constexpr const char* defaultReleasesUrl =
    "https://api.github.com/repos/hardwario/hio-monitor/releases?per_page=30";
// The page users are sent to when no release-specific page is known.
inline constexpr const char* releasesPageUrl = "https://github.com/hardwario/hio-monitor/releases";

struct ReleaseInfo {
    SemVer version;
    QString tag;           // "v2.1.0"
    QUrl htmlUrl;          // the release page
    QDateTime publishedAt; // UTC (invalid when GitHub sent none)
    bool prerelease = false;

    [[nodiscard]] bool operator==(const ReleaseInfo& other) const = default;
};

struct CheckResult {
    SemVer current; // the running version as compared (comparableCurrent())
    // The newest applicable release (may be older than or equal to `current`); nullopt when the
    // list holds no applicable v2 release.
    std::optional<ReleaseInfo> latest;
    // `latest` when it is newer than `current`.
    std::optional<ReleaseInfo> newer;

    // ADR-0177: Result<CheckResult> is a signal argument (Qt meta type).
    [[nodiscard]] bool operator==(const CheckResult& other) const = default;
};

// Errors: update.too_large (more than maxResponseBytes), update.bad_response (not a JSON array of
// objects; the reason in `detail`).
[[nodiscard]] Result<std::optional<ReleaseInfo>> pickLatest(QByteArrayView json,
                                                            bool includePrerelease);

// The CheckResult for `current` and the newest applicable release.
[[nodiscard]] CheckResult makeCheckResult(const SemVer& current, std::optional<ReleaseInfo> latest);

// defaultReleasesUrl, or HIO_UPDATE_URL in HIO_TEST_OVERRIDES builds (test-only override).
[[nodiscard]] QUrl releasesUrl();

} // namespace hio::update
