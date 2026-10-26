#include "update_commands.h"

#include "cli_app.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/core/version.h"
#include "hio/update/releases.h"
#include "hio/update/semver.h"
#include "hio/update/update_check.h"

#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtCore/qnamespace.h>

#include <CLI/CLI.hpp>

#include <chrono>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace hio::cli {

namespace {

constexpr std::chrono::milliseconds stopPollInterval{50};

struct CheckOptions {
    bool prerelease = false;
};

QJsonValue optionalString(const std::optional<QString>& text) {
    return text ? QJsonValue(*text) : QJsonValue(QJsonValue::Null);
}

Result<CommandResult> updateCheck(const Context& context, const CheckOptions& options) {
    const std::string running(buildInfo().version);
    auto current = update::SemVer::parse(running);
    if (!current) {
        return std::unexpected(
            makeError(ErrorCode::InternalUnexpected,
                      std::format("the running version '{}' is not a semantic version", running)));
    }
    const bool development = update::isDevelopmentBuild(*current);

    QNetworkAccessManager network;
    update::UpdateCheck check;
    // Ctrl+C (Context::stop) cancels the request.
    QTimer stopPoll;
    stopPoll.setInterval(stopPollInterval);
    QObject::connect(&stopPoll, &QTimer::timeout, &check, [&check, &context] {
        if (context.stop.stop_requested()) {
            check.cancel();
        }
    });
    stopPoll.start();
    check.start(network, update::releasesUrl(), *current, options.prerelease);
    auto result = update::waitForCheck(check);
    stopPoll.stop();
    if (!result) {
        return std::unexpected(std::move(result).error());
    }

    // The running version (without build metadata); development builds were compared by their
    // numeric part (update::comparableCurrent()).
    const std::string currentText = current->toString();
    const std::optional<update::ReleaseInfo>& latest = result->latest;
    QJsonObject data;
    data.insert(QStringLiteral("current"), toQString(currentText));
    data.insert(QStringLiteral("latest"),
                optionalString(latest ? std::optional(toQString(latest->version.toString()))
                                      : std::nullopt));
    data.insert(QStringLiteral("update_available"), result->newer.has_value());
    data.insert(QStringLiteral("prerelease"), latest ? latest->prerelease : false);
    data.insert(QStringLiteral("url"),
                optionalString(latest ? std::optional(latest->htmlUrl.toString()) : std::nullopt));
    data.insert(QStringLiteral("published_at"),
                optionalString(latest && latest->publishedAt.isValid()
                                   ? std::optional(latest->publishedAt.toString(Qt::ISODate))
                                   : std::nullopt));

    const std::string versionText = std::format("HARDWARIO Monitor {}{}", currentText,
                                                development ? " (development build)" : "");
    std::string human;
    if (result->newer) {
        human = std::format("{} — a newer version is available: {}\nDownload: {}", versionText,
                            result->newer->version.toString(),
                            toStdString(result->newer->htmlUrl.toString()));
    } else {
        human = std::format("{} — you have the latest version.", versionText);
    }
    return CommandResult{
        .data = data, .humanText = std::move(human), .streamed = false, .failure = std::nullopt};
}

} // namespace

void registerUpdateCommands(CLI::App& app, CommandSlot& slot) {
    CLI::App* group = app.add_subcommand("update", "Check for a newer HARDWARIO Monitor version");
    group->require_subcommand(1);
    auto options = std::make_shared<CheckOptions>();
    CLI::App* checkCommand = group->add_subcommand(
        "check", "Ask GitHub for the newest HARDWARIO Monitor release (nothing is downloaded or "
                 "installed; no data about you or your devices is sent)");
    checkCommand->add_flag("--prerelease", options->prerelease,
                           "Include pre-releases (beta, release candidates)");
    checkCommand->callback([&slot, options] {
        slot.command = [options](const Context& context) { return updateCheck(context, *options); };
    });
}

} // namespace hio::cli
