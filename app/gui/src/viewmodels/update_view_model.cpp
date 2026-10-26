#include "viewmodels/update_view_model.h"

#include "gui_log.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/update/releases.h"
#include "hio/update/semver.h"
#include "settings_store.h"
#include "update_service.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QLatin1Char>
#include <QLocale>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtCore/qnamespace.h>
#include <QtCore/qtmetamacros.h>
#include <QtLogging>

#include <functional>
#include <utility>

namespace hio::gui {

namespace {

const QString autoCheckKey = QStringLiteral("update/autoCheck");
const QString includePrereleaseKey = QStringLiteral("update/includePrerelease");
const QString skippedVersionKey = QStringLiteral("update/skippedVersion");
const QString lastCheckKey = QStringLiteral("update/lastCheck");
const QString lastResultKey = QStringLiteral("update/lastResult");

} // namespace

UpdateViewModel::UpdateViewModel(UpdateService& service, SettingsStore& settings,
                                 std::function<void(const QUrl&)> openUrl, QObject* parent)
    : QObject(parent), m_service(&service), m_settings(&settings), m_openUrl(std::move(openUrl)) {
    connect(m_service, &UpdateService::checkStarted, this, &UpdateViewModel::onStarted);
    connect(m_service, &UpdateService::checkFinished, this, &UpdateViewModel::onFinished);
    connect(m_settings, &SettingsStore::valueChanged, this, [this](const QString& key) {
        if (key == autoCheckKey || key == includePrereleaseKey) {
            emit settingsChanged();
        } else if (key == lastCheckKey || key == lastResultKey) {
            emit changed();
        }
    });
}

QString UpdateViewModel::currentVersion() const {
    return toQString(m_service->current().toString());
}

QString UpdateViewModel::lastChecked() const {
    const QDateTime last =
        QDateTime::fromString(m_settings->value(lastCheckKey).toString(), Qt::ISODate);
    if (!last.isValid()) {
        return {};
    }
    return QLocale().toString(last.toLocalTime(), QLocale::ShortFormat);
}

bool UpdateViewModel::autoCheck() const { return m_settings->value(autoCheckKey).toBool(); }

void UpdateViewModel::setAutoCheck(bool on) {
    if (const Status status = m_settings->setValue(autoCheckKey, on); !status) {
        emit settingsChanged(); // the control shows the stored value again
    }
}

bool UpdateViewModel::includePrerelease() const {
    return m_settings->value(includePrereleaseKey).toBool();
}

void UpdateViewModel::setIncludePrerelease(bool on) {
    if (const Status status = m_settings->setValue(includePrereleaseKey, on); !status) {
        emit settingsChanged();
    }
}

QString UpdateViewModel::statusText() const {
    switch (m_state) {
    case State::Checking:
        return tr("Checking for updates…");
    case State::UpToDate:
        return tr("You have the latest version.");
    case State::Available:
        return tr("Version %1 is available.").arg(m_latestVersion);
    case State::Failed:
        return m_errorText;
    case State::Idle:
        break;
    }
    // Nothing checked in this run: the result of the last check (update/lastResult).
    if (lastChecked().isEmpty()) {
        return {};
    }
    const auto latest =
        update::SemVer::parse(toStdString(m_settings->value(lastResultKey).toString()));
    if (latest && *latest > update::comparableCurrent(m_service->current())) {
        return tr("Version %1 is available.").arg(toQString(latest->toString()));
    }
    return tr("You have the latest version.");
}

QString UpdateViewModel::bannerText() const {
    return tr("HARDWARIO Monitor %1 is available (you have %2).")
        .arg(m_latestVersion, currentVersion());
}

void UpdateViewModel::checkNow() { m_service->checkNow(); }

void UpdateViewModel::download() {
    if (m_downloadUrl.isEmpty()) {
        return;
    }
    if (m_openUrl) {
        m_openUrl(m_downloadUrl);
    } else {
        QDesktopServices::openUrl(m_downloadUrl);
    }
}

void UpdateViewModel::skip() {
    if (!m_latestVersion.isEmpty()) {
        if (const Status status = m_settings->setValue(skippedVersionKey, m_latestVersion);
            !status) {
            qCWarning(lcGui).noquote()
                << "update/skippedVersion not stored:" << toQString(status.error().message);
        }
    }
    m_bannerVisible = false;
    emit changed();
}

void UpdateViewModel::dismiss() {
    m_bannerVisible = false;
    emit changed();
}

void UpdateViewModel::retranslate() { emit changed(); }

void UpdateViewModel::onStarted(bool automatic) {
    if (!automatic) {
        setState(State::Checking);
    }
}

void UpdateViewModel::onFinished(bool automatic, const Result<update::CheckResult>& result) {
    if (!result) {
        if (automatic) {
            return; // logged by UpdateService, never shown (ADR-0116)
        }
        const Error& error = result.error();
        m_errorText = toQString(error.message);
        if (!error.suggestion.empty()) {
            m_errorText += QLatin1Char(' ') + toQString(error.suggestion);
        }
        setState(State::Failed);
        return;
    }
    const update::CheckResult& check = *result;
    m_errorText.clear();
    if (check.newer) {
        const update::ReleaseInfo& newer = *check.newer;
        m_latestVersion = toQString(newer.version.toString());
        m_downloadUrl = newer.htmlUrl;
        const bool skipped = m_settings->value(skippedVersionKey).toString() == m_latestVersion;
        if (automatic && !skipped) {
            m_bannerVisible = true;
        }
        setState(State::Available);
    } else {
        m_latestVersion = check.latest ? toQString(check.latest->version.toString()) : QString();
        m_downloadUrl = check.latest ? check.latest->htmlUrl : QUrl();
        setState(State::UpToDate);
    }
}

void UpdateViewModel::setState(State state) {
    m_state = state;
    emit changed();
}

} // namespace hio::gui
