#include "viewmodels/settings_view_model.h"

#include "gui_log.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/device_session.h"
#include "hio/device/profile.h"
#include "pages/firmware_services.h"
#include "settings_store.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/dependency_status.h"
#include "viewmodels/page_registry.h"
#include "workers/firmware_loader.h"

#include <QLatin1Char>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtTypes>

#include <filesystem>

namespace hio::gui {

namespace {

const QString restoreTabsKey = QStringLiteral("ui/restoreTabs");
const QString speedKey = QStringLiteral("jlink/speedKhz");
const QString latencyKey = QStringLiteral("rtt/latencyMs");
const QString autoReconnectKey = QStringLiteral("rtt/autoReconnect");
const QString resetOnOpenKey = QStringLiteral("rtt/resetOnOpen");

// The RTT latency used when rtt/latencyMs is 0 (the profile's value; the first profile's console
// target, CHESTER app).
int profileLatencyMs() {
    const auto profiles = device::ProfileRegistry::all();
    if (profiles.empty() || profiles.front()->targets.empty()) {
        return device::minLatencyMs;
    }
    return profiles.front()->targets.front().defaultLatencyMs;
}

QString pathOf(const QUrl& file) {
    return file.isLocalFile() ? file.toLocalFile() : file.toString();
}

} // namespace

SettingsViewModel::SettingsViewModel(AppViewModel& app, FirmwareServices& services, QObject* parent)
    : QObject(parent), m_app(app), m_services(services) {
    loadSpeed();
    connect(&m_app, &AppViewModel::languageChanged, this, &SettingsViewModel::retranslate);
    connect(&m_app, &AppViewModel::themeChanged, this, &SettingsViewModel::themeChanged);
    connect(m_app.settingsStore(), &SettingsStore::valueChanged, this, [this](const QString& key) {
        if (key == speedKey) {
            loadSpeed();
            valuesChanged(); // signal
        } else if (key == restoreTabsKey || key == latencyKey || key == autoReconnectKey ||
                   key == resetOnOpenKey) {
            valuesChanged(); // signal
        } else if (key == FirmwareServices::jlinkPathKey) {
            jlinkLibraryChanged(); // signal
        } else if (key == FirmwareServices::nrfUtilPathKey) {
            nrfUtilChanged(); // signal
        }
    });
    DependencyStatus* status = m_services.dependencies();
    connect(status, &DependencyStatus::jlinkChanged, this, &SettingsViewModel::aboutChanged);
    connect(status, &DependencyStatus::nrfUtilChanged, this, &SettingsViewModel::aboutChanged);
    connect(status, &DependencyStatus::jlinkCandidateChecked, this,
            &SettingsViewModel::onJLinkCandidate);
    connect(status, &DependencyStatus::nrfUtilCandidateChecked, this,
            &SettingsViewModel::onNrfUtilCandidate);
}

bool SettingsViewModel::readOnly() const { return m_app.settingsStore()->readOnly(); }

QVariantList SettingsViewModel::sections() const {
    QVariantList list;
    for (const SettingsSectionDescriptor& section : m_app.pageRegistry().settingsSections()) {
        list.append(QVariantMap{{QStringLiteral("sectionId"), section.id},
                                {QStringLiteral("title"), PageRegistry::title(section)},
                                {QStringLiteral("qml"), section.qml}});
    }
    return list;
}

bool SettingsViewModel::write(const QString& key, const QVariant& value) {
    const Status status = m_app.settingsStore()->setValue(key, value);
    const QString error = status ? QString() : toQString(status.error().message);
    if (!status) {
        qCWarning(lcGui) << "Settings: could not store" << key << toQString(status.error().detail);
    }
    if (error != m_writeError) {
        m_writeError = error;
        writeErrorChanged(); // signal
    }
    return static_cast<bool>(status);
}

// ---- General
// ---------------------------------------------------------------------------------------

QString SettingsViewModel::language() const { return m_app.language(); }

void SettingsViewModel::setLanguage(const QString& code) {
    if (code != language()) {
        m_app.setLanguage(code); // live (LanguageManager writes ui/language)
    }
}

QVariantList SettingsViewModel::languages() const {
    QVariantList list;
    for (const QVariant& entry : m_app.availableLanguages()) {
        const QVariantMap map = entry.toMap();
        const QString name = map.value(QStringLiteral("nativeName")).toString();
        list.append(QVariantMap{{QStringLiteral("code"), map.value(QStringLiteral("code"))},
                                {QStringLiteral("name"), name.isEmpty() ? tr("System") : name}});
    }
    return list;
}

int SettingsViewModel::languageIndex() const {
    const QVariantList list = languages();
    for (qsizetype i = 0; i < list.size(); ++i) {
        if (list.at(i).toMap().value(QStringLiteral("code")).toString() == language()) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

void SettingsViewModel::selectLanguage(int index) {
    const QVariantList list = languages();
    if (index >= 0 && index < list.size()) {
        setLanguage(list.at(index).toMap().value(QStringLiteral("code")).toString());
    }
}

void SettingsViewModel::selectTheme(int index) {
    const QVariantList list = themes();
    if (index >= 0 && index < list.size()) {
        setTheme(list.at(index).toMap().value(QStringLiteral("code")).toString());
    }
}

QString SettingsViewModel::theme() const { return m_app.theme(); }

QVariantList SettingsViewModel::themes() {
    return {QVariantMap{{QStringLiteral("code"), QStringLiteral("system")},
                        {QStringLiteral("name"), tr("System")}},
            QVariantMap{{QStringLiteral("code"), QStringLiteral("light")},
                        {QStringLiteral("name"), tr("Light")}},
            QVariantMap{{QStringLiteral("code"), QStringLiteral("dark")},
                        {QStringLiteral("name"), tr("Dark")}}};
}

int SettingsViewModel::themeIndex() const {
    const QVariantList list = themes();
    for (qsizetype i = 0; i < list.size(); ++i) {
        if (list.at(i).toMap().value(QStringLiteral("code")).toString() == theme()) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

void SettingsViewModel::setTheme(const QString& theme) { m_app.setTheme(theme); }

bool SettingsViewModel::restoreTabs() const {
    return m_app.settingsStore()->value(restoreTabsKey).toBool();
}

void SettingsViewModel::setRestoreTabs(bool restore) {
    if (restore != restoreTabs()) {
        write(restoreTabsKey, restore);
    }
    valuesChanged(); // signal
}

// ---- J-Link
// ----------------------------------------------------------------------------------------

QString SettingsViewModel::jlinkLibraryMode() const {
    return m_jlinkCustomMode || !jlinkLibraryPath().isEmpty() ? QStringLiteral("custom")
                                                              : QStringLiteral("auto");
}

void SettingsViewModel::setJlinkLibraryMode(const QString& mode) {
    m_jlinkPathError.clear();
    m_jlinkCandidate = 0;
    if (mode == QStringLiteral("auto")) {
        m_jlinkCustomMode = false;
        if (!jlinkLibraryPath().isEmpty()) {
            write(FirmwareServices::jlinkPathKey, QString());
        }
    } else {
        m_jlinkCustomMode = true;
    }
    jlinkLibraryChanged(); // signal
}

QString SettingsViewModel::jlinkLibraryPath() const {
    return m_app.settingsStore()->value(FirmwareServices::jlinkPathKey).toString();
}

void SettingsViewModel::setJlinkLibraryPath(const QUrl& file) {
    const QString path = pathOf(file);
    if (path.isEmpty()) {
        return;
    }
    m_jlinkCustomMode = true;
    m_jlinkPathError.clear();
    m_jlinkCandidatePath = path;
    m_jlinkCandidate = m_services.dependencies()->checkJLinkCandidate(
        std::filesystem::path(path.toStdU16String()));
    jlinkLibraryChanged(); // signal
}

void SettingsViewModel::onJLinkCandidate(quint64 generation, const Result<JLinkFound>& result) {
    if (generation != m_jlinkCandidate) {
        return;
    }
    m_jlinkCandidate = 0;
    if (result) {
        m_jlinkPathError.clear();
        write(FirmwareServices::jlinkPathKey, m_jlinkCandidatePath);
    } else {
        // Inline at the control; nothing is saved (plan: dependency.jlink_load_failed, ...).
        m_jlinkPathError = toQString(result.error().message);
    }
    jlinkLibraryChanged(); // signal
}

void SettingsViewModel::loadSpeed() {
    m_speed.applyStoredSpeed(
        ConnectionOptions::storedSpeed(m_app.settingsStore()->value(speedKey)));
}

void SettingsViewModel::setAutoSpeed(bool automatic) {
    if (automatic != m_speed.autoSpeed) {
        m_speed.autoSpeed = automatic;
        write(speedKey, m_speed.speedToStore());
    }
    valuesChanged(); // signal
}

void SettingsViewModel::setDefaultSpeedKhz(int speed) {
    if (speed >= device::minSpeedKhz && speed <= device::maxSpeedKhz && speed != m_speed.speedKhz) {
        m_speed.speedKhz = speed;
        if (!m_speed.autoSpeed) {
            write(speedKey, m_speed.speedToStore());
        }
    }
    valuesChanged(); // signal: an invalid value shows the stored one again
}

int SettingsViewModel::defaultLatencyMs() const {
    const int stored = m_app.settingsStore()->value(latencyKey).toInt();
    return stored != 0 ? stored : profileLatencyMs();
}

void SettingsViewModel::setDefaultLatencyMs(int latency) {
    if (latency >= device::minLatencyMs && latency <= device::maxLatencyMs &&
        latency != defaultLatencyMs()) {
        write(latencyKey, latency);
    }
    valuesChanged(); // signal
}

bool SettingsViewModel::autoReconnect() const {
    return m_app.settingsStore()->value(autoReconnectKey).toBool();
}

void SettingsViewModel::setAutoReconnect(bool value) {
    if (value != autoReconnect()) {
        write(autoReconnectKey, value);
    }
    valuesChanged(); // signal
}

bool SettingsViewModel::resetOnConnect() const {
    return m_app.settingsStore()->value(resetOnOpenKey).toBool();
}

void SettingsViewModel::setResetOnConnect(bool value) {
    if (value != resetOnConnect()) {
        write(resetOnOpenKey, value);
    }
    valuesChanged(); // signal
}

int SettingsViewModel::minSpeedKhz() { return device::minSpeedKhz; }
int SettingsViewModel::maxSpeedKhz() { return device::maxSpeedKhz; }
int SettingsViewModel::minLatencyMs() { return device::minLatencyMs; }
int SettingsViewModel::maxLatencyMs() { return device::maxLatencyMs; }

// ---- nRF Util
// --------------------------------------------------------------------------------------

QString SettingsViewModel::nrfUtilMode() const {
    return m_nrfUtilCustomMode || !nrfUtilPath().isEmpty() ? QStringLiteral("custom")
                                                           : QStringLiteral("auto");
}

void SettingsViewModel::setNrfUtilMode(const QString& mode) {
    m_nrfUtilPathError.clear();
    m_nrfUtilCandidate = 0;
    if (mode == QStringLiteral("auto")) {
        m_nrfUtilCustomMode = false;
        if (!nrfUtilPath().isEmpty()) {
            write(FirmwareServices::nrfUtilPathKey, QString());
        }
    } else {
        m_nrfUtilCustomMode = true;
    }
    nrfUtilChanged(); // signal
}

QString SettingsViewModel::nrfUtilPath() const {
    return m_app.settingsStore()->value(FirmwareServices::nrfUtilPathKey).toString();
}

void SettingsViewModel::setNrfUtilPath(const QUrl& file) {
    const QString path = pathOf(file);
    if (path.isEmpty()) {
        return;
    }
    m_nrfUtilCustomMode = true;
    m_nrfUtilPathError.clear();
    m_nrfUtilCandidatePath = path;
    m_nrfUtilCandidate = m_services.dependencies()->checkNrfUtilCandidate(
        std::filesystem::path(path.toStdU16String()));
    nrfUtilChanged(); // signal
}

void SettingsViewModel::onNrfUtilCandidate(quint64 generation, const NrfUtilStatus& status) {
    if (generation != m_nrfUtilCandidate) {
        return;
    }
    m_nrfUtilCandidate = 0;
    if (status.executable) {
        // An existing nRF Util is saved even when its versions are too old: the status shows that.
        m_nrfUtilPathError.clear();
        write(FirmwareServices::nrfUtilPathKey, m_nrfUtilCandidatePath);
    } else {
        m_nrfUtilPathError = status.info ? QString() : toQString(status.info.error().message);
    }
    nrfUtilChanged(); // signal
}

// ---- detection and About
// -----------------------------------------------------------------------------

DependencyStatus* SettingsViewModel::dependencies() const { return m_services.dependencies(); }

QString SettingsViewModel::jlinkStatusText() const {
    const DependencyStatus* status = m_services.dependencies();
    switch (status->jlinkState()) {
    case DependencyStatus::Found:
        return tr("Detected: %1 · %2 · %3")
            .arg(status->jlinkPath(), status->jlinkVersion(), status->jlinkSourceText());
    case DependencyStatus::Checking:
        return tr("Checking…");
    case DependencyStatus::Problem:
        return status->jlinkError().message() + QLatin1Char(' ') +
               status->jlinkError().suggestion();
    default:
        return {};
    }
}

bool SettingsViewModel::jlinkStatusProblem() const {
    return m_services.dependencies()->jlinkState() == DependencyStatus::Problem;
}

QString SettingsViewModel::nrfUtilStatusText() const {
    const DependencyStatus* status = m_services.dependencies();
    switch (status->nrfUtilState()) {
    case DependencyStatus::Found:
        return tr("Detected: %1 · %2").arg(status->nrfUtilPath(), status->nrfUtilVersion());
    case DependencyStatus::Checking:
        return tr("Checking…");
    case DependencyStatus::Problem:
        if (status->nrfUtilError().code() == QStringLiteral("dependency.nrfutil_not_found")) {
            return tr("Not found — needed only for LTE modem firmware");
        }
        return status->nrfUtilError().message() + QLatin1Char(' ') +
               status->nrfUtilError().suggestion();
    default:
        return {};
    }
}

bool SettingsViewModel::nrfUtilStatusProblem() const {
    return m_services.dependencies()->nrfUtilState() == DependencyStatus::Problem;
}

void SettingsViewModel::pageShown() { m_services.dependencies()->ensureChecked(); }

QString SettingsViewModel::aboutText() const {
    const DependencyStatus* status = m_services.dependencies();
    const auto toolLine = [](const QString& version, const QString& path, int state,
                             const ErrorInfo& error) {
        switch (state) {
        case DependencyStatus::Found:
            return QStringLiteral("%1 (%2)").arg(version, path);
        case DependencyStatus::Checking:
            return tr("checking…");
        case DependencyStatus::Problem:
            return error.message();
        default:
            return tr("not checked");
        }
    };
    QStringList lines;
    lines.append(QStringLiteral("HARDWARIO Monitor %1").arg(AppViewModel::version()));
    lines.append(tr("Build: %1").arg(AppViewModel::buildInfo()));
    lines.append(tr("J-Link Software: %1")
                     .arg(toolLine(status->jlinkVersion(), status->jlinkPath(),
                                   status->jlinkState(), status->jlinkError())));
    lines.append(tr("nRF Util: %1")
                     .arg(toolLine(status->nrfUtilVersion(), status->nrfUtilPath(),
                                   status->nrfUtilState(), status->nrfUtilError())));
    return lines.join(QLatin1Char('\n'));
}

QVariantList SettingsViewModel::licences() {
    const auto entry = [](const QString& name, const QString& licence, const QString& use) {
        return QVariantMap{{QStringLiteral("name"), name},
                           {QStringLiteral("licence"), licence},
                           {QStringLiteral("use"), use}};
    };
    return {
        entry(QStringLiteral("Qt 6"), QStringLiteral("LGPL-3.0"),
              tr("Application framework, linked dynamically")),
        entry(QStringLiteral("Inter"), QStringLiteral("OFL-1.1"), tr("User interface font")),
        entry(QStringLiteral("JetBrains Mono"), QStringLiteral("OFL-1.1"), tr("Console font")),
        entry(QStringLiteral("Material Symbols"), QStringLiteral("Apache-2.0"), tr("Icons")),
        entry(QStringLiteral("miniz"), QStringLiteral("MIT"), tr("Zip archives")),
        entry(QStringLiteral("replxx"), QStringLiteral("BSD"), tr("Command line editing (hio)")),
        entry(QStringLiteral("CLI11"), QStringLiteral("BSD-3-Clause"),
              tr("Command line parsing (hio)")),
    };
}

QString SettingsViewModel::documentationUrl() { return AppViewModel::documentationUrl(); }

QString SettingsViewModel::catalogUrl() {
    const device::DeviceProfile* chester = device::ProfileRegistry::find("chester");
    return chester != nullptr ? toQString(chester->firmwareCatalogUrl) : QString();
}

QString SettingsViewModel::sourceUrl() {
    return QStringLiteral("https://github.com/hardwario/hio-monitor");
}

QString SettingsViewModel::nrfUtilInstallUrl() {
    return QStringLiteral("https://www.nordicsemi.com/Products/Development-tools/nRF-Util");
}

void SettingsViewModel::copyAbout() {
    m_app.copyToClipboard(aboutText());
    m_app.showToast(tr("Copied."));
}

void SettingsViewModel::retranslate() {
    sectionsChanged();     // signal
    languageChanged();     // signal
    valuesChanged();       // signal: speed warning
    jlinkLibraryChanged(); // signal
    nrfUtilChanged();      // signal
    aboutChanged();        // signal
}

} // namespace hio::gui
