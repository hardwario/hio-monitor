#include "hio/update/releases.h"

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/update/semver.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QString>
#include <QStringLiteral>
#include <QUrl>
#include <QtCore/qnamespace.h>
#include <QtEnvironmentVariables>

#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace hio::update {

namespace {

constexpr QLatin1StringView v2Prefix("v2.");

Error badResponse(std::string detail) {
    return makeError(ErrorCode::UpdateBadResponse, std::move(detail));
}

std::optional<ReleaseInfo> toRelease(const QJsonObject& entry, bool includePrerelease) {
    if (entry.value(QStringLiteral("draft")).toBool(false)) {
        return std::nullopt;
    }
    const QString tag = entry.value(QStringLiteral("tag_name")).toString();
    if (!tag.startsWith(v2Prefix)) {
        return std::nullopt; // v1 releases and other tags (ADR-0116)
    }
    auto version = SemVer::parse(tag.toStdString());
    if (!version || version->major != 2) {
        return std::nullopt;
    }
    const bool prerelease =
        entry.value(QStringLiteral("prerelease")).toBool(false) || version->isPrerelease();
    if (prerelease && !includePrerelease) {
        return std::nullopt;
    }
    QUrl htmlUrl(entry.value(QStringLiteral("html_url")).toString());
    if (!htmlUrl.isValid() || htmlUrl.scheme() != QStringLiteral("https")) {
        htmlUrl = QUrl(QString::fromLatin1(releasesPageUrl) + QStringLiteral("/tag/") + tag);
    }
    QDateTime publishedAt =
        QDateTime::fromString(entry.value(QStringLiteral("published_at")).toString(), Qt::ISODate);
    if (publishedAt.isValid()) {
        publishedAt = publishedAt.toUTC();
    }
    return ReleaseInfo{.version = std::move(*version),
                       .tag = tag,
                       .htmlUrl = htmlUrl,
                       .publishedAt = publishedAt,
                       .prerelease = prerelease};
}

} // namespace

Result<std::optional<ReleaseInfo>> pickLatest(QByteArrayView json, bool includePrerelease) {
    if (std::cmp_greater(json.size(), maxResponseBytes)) {
        return std::unexpected(
            makeError(ErrorCode::UpdateTooLarge,
                      std::format("the release list is larger than {} bytes", maxResponseBytes)));
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json.toByteArray(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return std::unexpected(
            badResponse(std::format("invalid JSON at offset {}: {}", parseError.offset,
                                    parseError.errorString().toStdString())));
    }
    if (!document.isArray()) {
        return std::unexpected(badResponse("the release list is not a JSON array"));
    }
    const QJsonArray releases = document.array();
    std::optional<ReleaseInfo> best;
    std::size_t index = 0;
    for (const auto& value : releases) {
        if (index++ >= maxReleases) {
            break;
        }
        if (!value.isObject()) {
            return std::unexpected(badResponse("a release list entry is not a JSON object"));
        }
        auto release = toRelease(value.toObject(), includePrerelease);
        if (release && (!best || release->version > best->version)) {
            best = std::move(release);
        }
    }
    return best;
}

CheckResult makeCheckResult(const SemVer& current, std::optional<ReleaseInfo> latest) {
    CheckResult result{
        .current = comparableCurrent(current), .latest = std::move(latest), .newer = std::nullopt};
    if (result.latest && result.latest->version > result.current) {
        result.newer = result.latest;
    }
    return result;
}

QUrl releasesUrl() {
#ifdef HIO_TEST_OVERRIDES
    // Test builds only (CMake option HIO_TEST_OVERRIDES; like HIO_CATALOG_URL).
    const QString override = qEnvironmentVariable("HIO_UPDATE_URL");
    if (!override.isEmpty()) {
        return {override};
    }
#endif
    return {QString::fromLatin1(defaultReleasesUrl)};
}

} // namespace hio::update
