#include "update_service.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/core/version.h"
#include "hio/update/releases.h"
#include "hio/update/semver.h"
#include "hio/update/update_check.h"
#include "settings_store.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QVariant>
#include <QtCore/qnamespace.h>
#include <QtCore/qtmetamacros.h>
#include <QtLogging>

#include <array>
#include <chrono>
#include <utility>

namespace hio::gui {

namespace {

const QString autoCheckKey = QStringLiteral("update/autoCheck");
const QString includePrereleaseKey = QStringLiteral("update/includePrerelease");
const QString skippedVersionKey = QStringLiteral("update/skippedVersion");
const QString lastCheckKey = QStringLiteral("update/lastCheck");
const QString lastResultKey = QStringLiteral("update/lastResult");

update::SemVer buildVersion() {
    return update::SemVer::parse(buildInfo().version).value_or(update::SemVer{});
}

void logStatus(const char* what, const Status& status) {
    if (!status) {
        qCWarning(log::lcUpdate).noquote()
            << what << toQString(status.error().message) << toQString(status.error().detail);
    }
}

} // namespace

UpdateService::UpdateService(SettingsStore& settings, QNetworkAccessManager& network,
                             Options options, QObject* parent)
    : QObject(parent), m_settings(&settings), m_network(&network), m_options(std::move(options)),
      m_current(m_options.current.value_or(buildVersion())) {
    if (m_options.url.isEmpty()) {
        m_options.url = update::releasesUrl();
    }
    if (m_options.packageKind.isEmpty()) {
        m_options.packageKind = toQString(buildInfo().packageKind);
    }
    m_startTimer.setSingleShot(true);
    m_startTimer.setInterval(m_options.startDelay);
    connect(&m_startTimer, &QTimer::timeout, this, [this] {
        if (automaticCheckDue()) {
            runCheck(true);
        } else {
            qCInfo(log::lcUpdate) << "Automatic update check not due";
        }
    });
    connect(&m_check, &update::UpdateCheck::finished, this, &UpdateService::onFinished);
}

void UpdateService::registerKeys(SettingsStore& store, bool runningIsPrerelease) {
    const std::array keys{
        SettingKey{
            .path = autoCheckKey, .type = QMetaType::Bool, .defaultValue = true, .validator = {}},
        SettingKey{.path = includePrereleaseKey,
                   .type = QMetaType::Bool,
                   .defaultValue = runningIsPrerelease,
                   .validator = {}},
        SettingKey{.path = skippedVersionKey,
                   .type = QMetaType::QString,
                   .defaultValue = QString(),
                   .validator = validators::maxLength(64)},
        SettingKey{.path = lastCheckKey,
                   .type = QMetaType::QString,
                   .defaultValue = QString(),
                   .validator = validators::maxLength(64)},
        SettingKey{.path = lastResultKey,
                   .type = QMetaType::QString,
                   .defaultValue = QString(),
                   .validator = validators::maxLength(64)},
    };
    store.registerKeys(keys);
}

void UpdateService::start() {
    if (isDevelopmentBuild()) {
        qCInfo(log::lcUpdate) << "Development build: no automatic update check";
        return;
    }
    m_startTimer.start();
}

void UpdateService::checkNow() { runCheck(false); }

void UpdateService::checkAutomatically() { runCheck(true); }

bool UpdateService::isDevelopmentBuild() const {
    return m_options.packageKind == QStringLiteral("dev");
}

QDateTime UpdateService::now() const {
    return m_options.now ? m_options.now() : QDateTime::currentDateTimeUtc();
}

bool UpdateService::automaticCheckDue() const {
    if (isDevelopmentBuild() || !m_settings->value(autoCheckKey).toBool()) {
        return false;
    }
    const QDateTime last =
        QDateTime::fromString(m_settings->value(lastCheckKey).toString(), Qt::ISODate);
    if (!last.isValid()) {
        return true;
    }
    const auto elapsed = std::chrono::seconds(last.secsTo(now()));
    // A last check in the future (clock changed) counts as due.
    return elapsed < std::chrono::seconds(0) || elapsed >= m_options.interval;
}

void UpdateService::runCheck(bool automatic) {
    if (m_check.isRunning()) {
        qCInfo(log::lcUpdate) << "Update check already running";
        return;
    }
    m_automatic = automatic;
    const bool prerelease = m_settings->value(includePrereleaseKey).toBool();
    emit checkStarted(automatic);
    m_check.start(*m_network, m_options.url, m_current, prerelease);
}

void UpdateService::onFinished(const Result<update::CheckResult>& result) {
    const bool automatic = m_automatic;
    if (result) {
        logStatus("update/lastCheck not stored:",
                  m_settings->setValue(lastCheckKey, now().toUTC().toString(Qt::ISODate)));
        const update::CheckResult& check = *result;
        const QString latest =
            check.latest ? toQString(check.latest->version.toString()) : QString();
        logStatus("update/lastResult not stored:", m_settings->setValue(lastResultKey, latest));
    } else if (automatic) {
        qCWarning(log::lcUpdate).noquote()
            << "Automatic update check failed:" << toQString(idOf(result.error().code))
            << toQString(result.error().detail);
    }
    emit checkFinished(automatic, result);
}

} // namespace hio::gui
