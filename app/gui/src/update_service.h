#pragma once

// UpdateService - when and how the GUI checks for updates (plan/features/0016 C1, ADR-0116).
//
// Owns one hio::update::UpdateCheck and uses a QNetworkAccessManager owned by the caller
// (AppViewModel). start() schedules the automatic check: `startDelay` (10 s) after the start, if
// update/autoCheck is on, the build is not a development build (HIO_PACKAGE_KIND "dev") and the
// last check is at least `interval` (24 h) ago (or never happened). checkNow() is the manual check
// (Settings > About). One check at a time: a check requested while one runs is ignored.
//
// After a successful check update/lastCheck (UTC, ISO 8601) and update/lastResult (the newest
// applicable version, "" when there is none) are stored. Failed automatic checks are only logged
// (hio.update, warning); the view model shows failures of manual checks only.
//
// Setting keys (ADR-0025, registerKeys()): update/autoCheck (bool, true), update/includePrerelease
// (bool, false; true when the running version is itself a pre-release), update/skippedVersion
// (string), update/lastCheck (string), update/lastResult (string).
//
// The clock is injected (Options::now) for the tests. GUI thread only.

#include "hio/core/result.h"
#include "hio/update/releases.h"
#include "hio/update/semver.h"
#include "hio/update/update_check.h"
#include "settings_store.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <functional>
#include <optional>

class QNetworkAccessManager;

namespace hio::gui {

class UpdateService : public QObject {
    Q_OBJECT

public:
    struct Options {
        std::function<QDateTime()> now;              // UTC; empty: the system clock
        std::chrono::milliseconds startDelay{10000}; // ADR-0116 / plan 0016: 10 s
        std::chrono::hours interval{24};
        QUrl url;                              // empty: hio::update::releasesUrl()
        QString packageKind;                   // empty: the build's (BuildInfo::packageKind)
        std::optional<update::SemVer> current; // empty: the build's version
    };

    UpdateService(SettingsStore& settings, QNetworkAccessManager& network, Options options,
                  QObject* parent = nullptr);

    // The update/* keys; `runningIsPrerelease` makes update/includePrerelease default to true.
    static void registerKeys(SettingsStore& store, bool runningIsPrerelease);

    // Schedules the automatic check (once per start).
    void start();
    // The manual check (ignores update/autoCheck and the interval).
    void checkNow();
    // The automatic check at once, without the start delay and the due check (the start timer
    // calls it when due; tests).
    void checkAutomatically();

    [[nodiscard]] bool isChecking() const noexcept { return m_check.isRunning(); }
    // The automatic check would run now (autoCheck, not a development build, interval elapsed).
    [[nodiscard]] bool automaticCheckDue() const;
    [[nodiscard]] const update::SemVer& current() const noexcept { return m_current; }
    [[nodiscard]] bool isDevelopmentBuild() const;
    [[nodiscard]] QDateTime now() const;

signals:
    void checkStarted(bool automatic);
    void checkFinished(bool automatic, const hio::Result<hio::update::CheckResult>& result);

private:
    void runCheck(bool automatic);
    void onFinished(const Result<update::CheckResult>& result);

    SettingsStore* m_settings;        // non-owning, owned by AppViewModel (outlives this)
    QNetworkAccessManager* m_network; // non-owning, owned by AppViewModel (outlives this)
    Options m_options;
    update::SemVer m_current;
    update::UpdateCheck m_check;
    QTimer m_startTimer;
    bool m_automatic = false; // trigger of the running check
};

} // namespace hio::gui
