#pragma once

// UpdateViewModel - the update banner and the update block of Settings > About (plan/features/0016
// C2, ADR-0116). Exposed as AppViewModel.update.
//
// state: Idle (nothing checked in this run), Checking, UpToDate, Available, Failed (a manual check
// failed; automatic failures are only logged by UpdateService and leave the state unchanged).
// The banner (bannerVisible) shows a newer version found by the automatic check unless it is the
// skipped version (update/skippedVersion) or the user chose [Later] in this run; manual checks
// ignore the skipped version and are shown in About only. [Download] opens the release page in the
// browser (the app keeps running).
//
// Ownership: AppViewModel (Qt parent); the service and the settings store outlive it. GUI thread.

#include "hio/core/result.h"
#include "hio/update/releases.h"
#include "settings_store.h"
#include "update_service.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQmlIntegration>

#include <cstdint>
#include <functional>

namespace hio::gui {

class UpdateViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppViewModel")

    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QUrl downloadUrl READ downloadUrl NOTIFY changed)
    Q_PROPERTY(QString lastChecked READ lastChecked NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(bool bannerVisible READ bannerVisible NOTIFY changed)
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY settingsChanged)
    Q_PROPERTY(bool includePrerelease READ includePrerelease WRITE setIncludePrerelease NOTIFY
                   settingsChanged)
    // "You have the latest version." / "Version 2.1.0 is available." / the error (About).
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    // The banner text: "HARDWARIO Monitor 2.1.0 is available (you have 2.0.3)."
    Q_PROPERTY(QString bannerText READ bannerText NOTIFY changed)

public:
    enum class State : std::uint8_t { Idle, Checking, UpToDate, Available, Failed };
    Q_ENUM(State)

    // `openUrl` empty: QDesktopServices::openUrl (the tests pass their own).
    UpdateViewModel(UpdateService& service, SettingsStore& settings,
                    std::function<void(const QUrl&)> openUrl = {}, QObject* parent = nullptr);

    [[nodiscard]] State state() const noexcept { return m_state; }
    [[nodiscard]] QString latestVersion() const { return m_latestVersion; }
    [[nodiscard]] QString currentVersion() const;
    [[nodiscard]] QUrl downloadUrl() const { return m_downloadUrl; }
    [[nodiscard]] QString lastChecked() const;
    [[nodiscard]] QString errorText() const { return m_errorText; }
    [[nodiscard]] bool bannerVisible() const noexcept { return m_bannerVisible; }
    [[nodiscard]] bool autoCheck() const;
    void setAutoCheck(bool on);
    [[nodiscard]] bool includePrerelease() const;
    void setIncludePrerelease(bool on);
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString bannerText() const;

    // [Check now] (About).
    Q_INVOKABLE void checkNow();
    // [Download]: the release page in the browser.
    Q_INVOKABLE void download();
    // [Skip this version]: that version is never announced by the banner again.
    Q_INVOKABLE void skip();
    // [Later] (the banner's close button): hidden until the next automatic check.
    Q_INVOKABLE void dismiss();

    // Language switch: the translated texts change.
    void retranslate();

signals:
    void changed();
    void settingsChanged();

private:
    void onStarted(bool automatic);
    void onFinished(bool automatic, const Result<update::CheckResult>& result);
    void setState(State state);

    UpdateService* m_service;  // non-owning, owned by AppViewModel (outlives this)
    SettingsStore* m_settings; // non-owning, owned by AppViewModel (outlives this)
    std::function<void(const QUrl&)> m_openUrl;
    State m_state = State::Idle;
    QString m_latestVersion;
    QUrl m_downloadUrl;
    QString m_errorText;
    bool m_bannerVisible = false;
};

} // namespace hio::gui
