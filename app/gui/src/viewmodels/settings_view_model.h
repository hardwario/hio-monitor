#pragma once

// SettingsViewModel - the global Settings page (plan/features/0006-m3-flash-settings.md, E1;
// ADR-0025, ADR-0027, ADR-0047, ADR-0052, ADR-0059, ADR-0061, ADR-0090, ADR-0110, ADR-0111).
//
// One typed property per item; changes apply immediately (no OK/Cancel) and are written through
// the SettingsStore (validated; an invalid value shows an inline error and is not saved). The page
// hosts the sections of the PageRegistry (General, J-Link, nRF Util and About of this plan, the
// Console section of 0005) in their order.
//   - General: language (live), theme (live), reopen tabs at start (ui/restoreTabs).
//   - J-Link: library Automatic / Custom path (jlink/libraryPath; a custom path is checked -
//   located
//     and loaded once - before it is saved), the defaults of the ⋯ Advanced popover for probes
//     without their own values: speed with "Automatic speed" (ADR-0111: jlink/speedKhz, 0 =
//     automatic; the kHz box keeps the last fixed value, the ADR-0110 warning below 125 kHz; the
//     same ConnectionOptions logic as the popover), RTT latency (rtt/latencyMs, 0 = the profile's),
//     auto-reconnect, reset on connect.
//   - nRF Util: Automatic / Custom path (nrfutil/path, checked before it is saved).
//   - About: versions, licences, links; Copy.
// A settings file of a newer schema is read-only (readOnly: the page disables every control).
//
// Ownership: child of AppViewModel (global page view model). GUI thread only.

#include "viewmodels/connection_view_model.h"
#include "viewmodels/dependency_status.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QtQmlIntegration>
#include <QtTypes>

#include <cstdint>

namespace hio::gui {

class AppViewModel;
class FirmwareServices;

class SettingsViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the page registry")

    Q_PROPERTY(bool readOnly READ readOnly CONSTANT)
    Q_PROPERTY(QVariantList sections READ sections NOTIFY sectionsChanged)
    // An error of the last write ("" = none), shown at the top of the page.
    Q_PROPERTY(QString writeError READ writeError NOTIFY writeErrorChanged)

    // General
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QVariantList languages READ languages NOTIFY languageChanged)
    Q_PROPERTY(int languageIndex READ languageIndex NOTIFY languageChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QVariantList themes READ themes NOTIFY languageChanged) // [{code, name}]
    Q_PROPERTY(int themeIndex READ themeIndex NOTIFY themeChanged)
    Q_PROPERTY(bool restoreTabs READ restoreTabs WRITE setRestoreTabs NOTIFY valuesChanged)

    // J-Link
    Q_PROPERTY(QString jlinkLibraryMode READ jlinkLibraryMode WRITE setJlinkLibraryMode NOTIFY
                   jlinkLibraryChanged) // "auto" | "custom"
    Q_PROPERTY(QString jlinkLibraryPath READ jlinkLibraryPath NOTIFY jlinkLibraryChanged)
    Q_PROPERTY(QString jlinkPathError READ jlinkPathError NOTIFY jlinkLibraryChanged)
    Q_PROPERTY(bool jlinkPathChecking READ jlinkPathChecking NOTIFY jlinkLibraryChanged)
    Q_PROPERTY(bool autoSpeed READ autoSpeed WRITE setAutoSpeed NOTIFY valuesChanged)
    Q_PROPERTY(
        int defaultSpeedKhz READ defaultSpeedKhz WRITE setDefaultSpeedKhz NOTIFY valuesChanged)
    Q_PROPERTY(QString speedWarning READ speedWarning NOTIFY valuesChanged)
    Q_PROPERTY(
        int defaultLatencyMs READ defaultLatencyMs WRITE setDefaultLatencyMs NOTIFY valuesChanged)
    Q_PROPERTY(bool autoReconnect READ autoReconnect WRITE setAutoReconnect NOTIFY valuesChanged)
    Q_PROPERTY(bool resetOnConnect READ resetOnConnect WRITE setResetOnConnect NOTIFY valuesChanged)
    Q_PROPERTY(int minSpeedKhz READ minSpeedKhz CONSTANT)
    Q_PROPERTY(int maxSpeedKhz READ maxSpeedKhz CONSTANT)
    Q_PROPERTY(int minLatencyMs READ minLatencyMs CONSTANT)
    Q_PROPERTY(int maxLatencyMs READ maxLatencyMs CONSTANT)

    // nRF Util
    Q_PROPERTY(QString nrfUtilMode READ nrfUtilMode WRITE setNrfUtilMode NOTIFY nrfUtilChanged)
    Q_PROPERTY(QString nrfUtilPath READ nrfUtilPath NOTIFY nrfUtilChanged)
    Q_PROPERTY(QString nrfUtilPathError READ nrfUtilPathError NOTIFY nrfUtilChanged)
    Q_PROPERTY(bool nrfUtilPathChecking READ nrfUtilPathChecking NOTIFY nrfUtilChanged)

    // Detection (shared with the Flash page) and the setup wizard (0008: hidden until registered)
    Q_PROPERTY(hio::gui::DependencyStatus* dependencies READ dependencies CONSTANT)
    Q_PROPERTY(QString setupWizardId READ setupWizardId CONSTANT)
    // "Detected: <path> · <version> · <source>", "Checking…" or the problem (translated).
    Q_PROPERTY(QString jlinkStatusText READ jlinkStatusText NOTIFY aboutChanged)
    Q_PROPERTY(bool jlinkStatusProblem READ jlinkStatusProblem NOTIFY aboutChanged)
    Q_PROPERTY(QString nrfUtilStatusText READ nrfUtilStatusText NOTIFY aboutChanged)
    Q_PROPERTY(bool nrfUtilStatusProblem READ nrfUtilStatusProblem NOTIFY aboutChanged)

    // About
    Q_PROPERTY(QString aboutText READ aboutText NOTIFY aboutChanged)
    Q_PROPERTY(QVariantList licences READ licences NOTIFY languageChanged)
    Q_PROPERTY(QString documentationUrl READ documentationUrl CONSTANT)
    Q_PROPERTY(QString catalogUrl READ catalogUrl CONSTANT)
    Q_PROPERTY(QString sourceUrl READ sourceUrl CONSTANT)
    Q_PROPERTY(QString nrfUtilInstallUrl READ nrfUtilInstallUrl CONSTANT)

public:
    static inline const QString pageId = QStringLiteral("settings");

    SettingsViewModel(AppViewModel& app, FirmwareServices& services, QObject* parent);

    [[nodiscard]] bool readOnly() const;
    [[nodiscard]] QVariantList sections() const;
    [[nodiscard]] QString writeError() const { return m_writeError; }

    [[nodiscard]] QString language() const;
    void setLanguage(const QString& code);
    [[nodiscard]] QVariantList languages() const;
    [[nodiscard]] int languageIndex() const;
    // The ComboBoxes' choices (index into languages / themes).
    Q_INVOKABLE void selectLanguage(int index);
    Q_INVOKABLE void selectTheme(int index);
    [[nodiscard]] QString theme() const;
    [[nodiscard]] static QVariantList themes();
    [[nodiscard]] int themeIndex() const;
    void setTheme(const QString& theme);
    [[nodiscard]] bool restoreTabs() const;
    void setRestoreTabs(bool restore);

    [[nodiscard]] QString jlinkLibraryMode() const;
    void setJlinkLibraryMode(const QString& mode);
    [[nodiscard]] QString jlinkLibraryPath() const;
    [[nodiscard]] QString jlinkPathError() const { return m_jlinkPathError; }
    [[nodiscard]] bool jlinkPathChecking() const { return m_jlinkCandidate != 0; }
    // Checks the file and saves it as jlink/libraryPath when it loads (inline error otherwise).
    Q_INVOKABLE void setJlinkLibraryPath(const QUrl& file);
    [[nodiscard]] bool autoSpeed() const noexcept { return m_speed.autoSpeed; }
    void setAutoSpeed(bool automatic);
    [[nodiscard]] int defaultSpeedKhz() const noexcept { return m_speed.speedKhz; }
    void setDefaultSpeedKhz(int speed);
    [[nodiscard]] QString speedWarning() const { return m_speed.speedWarning(); }
    [[nodiscard]] int defaultLatencyMs() const;
    void setDefaultLatencyMs(int latency);
    [[nodiscard]] bool autoReconnect() const;
    void setAutoReconnect(bool value);
    [[nodiscard]] bool resetOnConnect() const;
    void setResetOnConnect(bool value);
    [[nodiscard]] static int minSpeedKhz();
    [[nodiscard]] static int maxSpeedKhz();
    [[nodiscard]] static int minLatencyMs();
    [[nodiscard]] static int maxLatencyMs();

    [[nodiscard]] QString nrfUtilMode() const;
    void setNrfUtilMode(const QString& mode);
    [[nodiscard]] QString nrfUtilPath() const;
    [[nodiscard]] QString nrfUtilPathError() const { return m_nrfUtilPathError; }
    [[nodiscard]] bool nrfUtilPathChecking() const { return m_nrfUtilCandidate != 0; }
    Q_INVOKABLE void setNrfUtilPath(const QUrl& file);

    [[nodiscard]] DependencyStatus* dependencies() const;
    [[nodiscard]] static QString setupWizardId() { return QStringLiteral("first-run-setup"); }
    [[nodiscard]] QString jlinkStatusText() const;
    [[nodiscard]] bool jlinkStatusProblem() const;
    [[nodiscard]] QString nrfUtilStatusText() const;
    [[nodiscard]] bool nrfUtilStatusProblem() const;

    [[nodiscard]] QString aboutText() const;
    [[nodiscard]] static QVariantList licences();
    [[nodiscard]] static QString documentationUrl();
    [[nodiscard]] static QString catalogUrl();
    [[nodiscard]] static QString sourceUrl();
    [[nodiscard]] static QString nrfUtilInstallUrl();

    // The page became visible: detect the tools once (cached afterwards).
    Q_INVOKABLE void pageShown();
    Q_INVOKABLE void copyAbout();

signals:
    void sectionsChanged();
    void writeErrorChanged();
    void languageChanged();
    void themeChanged();
    void valuesChanged();
    void jlinkLibraryChanged();
    void nrfUtilChanged();
    void aboutChanged();

private:
    // Writes a value; a failure is shown in writeError (and logged).
    bool write(const QString& key, const QVariant& value);
    void loadSpeed();
    void onJLinkCandidate(quint64 generation, const Result<JLinkFound>& result);
    void onNrfUtilCandidate(quint64 generation, const NrfUtilStatus& status);
    void retranslate();

    AppViewModel& m_app;
    FirmwareServices& m_services;
    ConnectionOptions m_speed; // the default speed: automatic + the last fixed value
    bool m_jlinkCustomMode = false;
    bool m_nrfUtilCustomMode = false;
    std::uint64_t m_jlinkCandidate = 0;
    std::uint64_t m_nrfUtilCandidate = 0;
    QString m_jlinkCandidatePath;
    QString m_nrfUtilCandidatePath;
    QString m_jlinkPathError;
    QString m_nrfUtilPathError;
    QString m_writeError;
};

} // namespace hio::gui
