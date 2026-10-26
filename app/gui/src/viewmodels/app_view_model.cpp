#include "viewmodels/app_view_model.h"

#include "gui_log.h"
#include "hio/core/crash_handler.h"
#include "hio/core/i18n.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/core/settings.h"
#include "hio/core/version.h"
#include "hio/device/device_session.h"
#include "hio/device/session_registry.h"
#include "hio/diag/environment.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/types.h"
#include "hio/hw/nrfutil/nrfutil.h"
#include "language_manager.h"
#include "report_view_model.h"
#include "settings_store.h"
#include "system_preferences.h"
#include "viewmodels/device_tab_list_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/history_store.h"
#include "viewmodels/page_list_model.h"
#include "viewmodels/page_registry.h"
#include "viewmodels/shortcut_registry.h"
#include "wizard_launcher.h"
#include "wizard_view_model.h"
#include "workers/diag_worker.h"
#include "workers/probe_scanner.h"

#include <QByteArray>
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QKeySequence>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QMetaType>
#include <QObject>
#include <QQmlEngine>
#include <QRect>
#include <QScreen>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QWindow>
#include <QtContainerFwd>
#include <QtCore/qnamespace.h>
#include <QtCore/qtmetamacros.h>
#include <QtCore/qttranslation.h>
#include <QtTypes>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace hio::gui {

namespace {

const QString themeKey = QStringLiteral("ui/theme");
const QString geometryKey = QStringLiteral("ui/windowGeometry");
const QString restoreTabsKey = QStringLiteral("ui/restoreTabs");
const QString tabsKey = QStringLiteral("ui/tabs");

constexpr int minWindowWidth = 960;
constexpr int minWindowHeight = 600;

QStringList languageCodes() {
    QStringList codes{QStringLiteral("system")};
    for (const i18n::LanguageInfo& info : i18n::supportedLanguages) {
        codes.append(toQString(info.code));
    }
    return codes;
}

void logFailure(const char* what, const Status& status) {
    if (!status) {
        qCWarning(lcGui) << "Could not store" << what << toQString(status.error().message)
                         << toQString(status.error().detail);
    }
}

} // namespace

AppViewModel::AppViewModel(AppOptions options, QObject* parent)
    : QObject(parent), m_options(std::move(options)),
      // Qt parent ownership: the store, the shortcut registry, the scanner and the tab list are
      // children of this object (destroyed explicitly in the right order by the destructor).
      m_settings(new SettingsStore( // NOLINT(cppcoreguidelines-owning-memory)
          std::move(m_options.settings), this)),
      m_shortcuts(new ShortcutRegistry(this)),      // NOLINT(cppcoreguidelines-owning-memory)
      m_pageList(new PageListModel(m_pages, this)), // NOLINT(cppcoreguidelines-owning-memory)
      m_registry(std::make_unique<device::SessionRegistry>(m_options.registry)),
      m_scanner(std::make_unique<ProbeScanner>(m_options.registry.instanceRoot,
                                               m_options.registry.lockDirectory,
                                               m_options.scanInterval)),
      m_diagWorker(std::make_unique<DiagWorker>()),
      m_wizard(new WizardViewModel( // NOLINT(cppcoreguidelines-owning-memory)
          m_wizardRegistry, this)),
      m_launcher(new WizardLauncher( // NOLINT(cppcoreguidelines-owning-memory)
          m_wizardRegistry, *m_wizard,
          [this](int index) { return index < 0 ? currentTabViewModel() : m_tabs->at(index); },
          this)),
      m_report(new ReportViewModel( // NOLINT(cppcoreguidelines-owning-memory)
          *m_diagWorker,
          ReportViewModel::Hooks{.environment = [this] { return diagEnvironment(); },
                                 .tabs = [this] { return tabsJson(); },
                                 .language = [this] { return language(); },
                                 .toast = [this](const QString& text) { showToast(text); },
                                 .openFolder = {}},
          this)),
      m_history(std::make_unique<HistoryStore>(m_options.registry.consoleDirectory)),
      m_tabs(new DeviceTabListModel(this)), // NOLINT(cppcoreguidelines-owning-memory)
      m_reducedMotion(reducedMotionRequested()) {
    m_availableLanguages.append(QVariantMap{{QStringLiteral("code"), QStringLiteral("system")},
                                            {QStringLiteral("nativeName"), QString()}});
    for (const i18n::LanguageInfo& info : i18n::supportedLanguages) {
        m_availableLanguages.append(
            QVariantMap{{QStringLiteral("code"), toQString(info.code)},
                        {QStringLiteral("nativeName"), toQString(info.nativeName)}});
    }

    registerShellKeys(*m_settings);
    registerShellShortcuts();
    // The plans' pages, sections, keys and shortcuts: before the first tab exists (contract).
    if (m_options.registerExtensions) {
        m_options.registerExtensions(*this);
    }
    // Ctrl+1, Ctrl+2, ... for the per-tab pages in rail order (ADR-0051).
    int pageNumber = 1;
    for (const PageDescriptor& page : m_pages.pages()) {
        m_shortcuts->setScopeTitle(page.id, page.titleSourceText);
        if (page.global || pageNumber > 9) {
            continue;
        }
        logFailure("page shortcut",
                   m_shortcuts->add(QStringLiteral("page.") + page.id,
                                    QKeySequence(QStringLiteral("Ctrl+%1").arg(pageNumber)),
                                    ShortcutRegistry::globalScope, page.titleSourceText));
        ++pageNumber;
    }
    createGlobalPages();

    refreshCrashNotice();

    connect(m_scanner.get(), &ProbeScanner::scanned, this, &AppViewModel::onScan);
    connect(m_wizard, &WizardViewModel::handOff, this, &AppViewModel::onHandOff);
    connect(m_wizard, &WizardViewModel::copied, this, [this] { showToast(tr("Copied.")); });
    connect(m_settings, &SettingsStore::valueChanged, this, [this](const QString& key) {
        if (key == QStringLiteral("jlink/libraryPath")) {
            updateLocatorOptions();
        } else if (key == themeKey) {
            themeChanged(); // signal
        }
    });
    updateLocatorOptions();
    // The library path for the Settings placeholder (a file-system lookup, no library call).
    if (const auto location = jlink::locateLibrary(locatorOptions()); location) {
        m_jlinkLibraryPath = toQString(toUtf8(location->path));
    }

    restoreTabs();
}

AppViewModel::~AppViewModel() {
    // The open wizard first (it may use a tab's session): its operation is cancelled.
    if (m_wizard != nullptr) {
        m_wizard->setParent(nullptr);
        const std::unique_ptr<WizardViewModel> owned(m_wizard);
        m_wizard = nullptr;
    }
    // Tabs first: their sessions use the registry, their pickers the scanner.
    while (m_tabs->count() > 0) {
        std::unique_ptr<DeviceTabViewModel> tab = m_tabs->take(m_tabs->count() - 1);
        tab->shutdown();
    }
    // Then the remaining children that use the members (review S12): closed tabs still waiting
    // for their deleteLater() and the global page view models. The store, the shortcut registry
    // and the models are deleted by QObject after the members.
    const QObjectList current = children(); // a copy: setParent() changes children()
    for (QObject* child : current) {
        if (qobject_cast<DeviceTabViewModel*>(child) != nullptr) {
            child->setParent(nullptr);
            const std::unique_ptr<QObject> owned(child); // deleted now, not by QObject later
        }
    }
    for (auto& [pageId, viewModel] : m_globalViewModels) {
        if (viewModel != nullptr && viewModel->parent() == this) {
            viewModel->setParent(nullptr);
            const std::unique_ptr<QObject> owned(viewModel);
        }
        viewModel = nullptr;
    }
    // The members follow: the scanner's worker is joined, then the registry is destroyed.
}

AppViewModel* AppViewModel::create(QQmlEngine* qmlEngine, QJSEngine* /*jsEngine*/) {
    auto* instance = QCoreApplication::instance()->findChild<AppViewModel*>(
        QString(), Qt::FindDirectChildrenOnly);
    if (instance != nullptr) {
        QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
        return instance;
    }
    qCWarning(lcGui) << "AppViewModel: no instance created by main(); using defaults";
    // Qt parent ownership: the engine owns this fallback instance.
    return new AppViewModel( // NOLINT(cppcoreguidelines-owning-memory)
        AppOptions{.settings = std::nullopt,
                   .registry = device::RegistryOptions::defaults(),
                   .scanInterval = ProbeScanner::defaultInterval,
                   .registerExtensions = {},
                   .crashDirectory = {}},
        qmlEngine);
}

void AppViewModel::registerShellKeys(SettingsStore& store) {
    const std::array keys{
        SettingKey{.path = QStringLiteral("ui/language"),
                   .type = QMetaType::QString,
                   .defaultValue = QStringLiteral("system"),
                   .validator = validators::oneOf(languageCodes())},
        SettingKey{.path = themeKey,
                   .type = QMetaType::QString,
                   .defaultValue = QStringLiteral("system"),
                   .validator =
                       validators::oneOf({QStringLiteral("system"), QStringLiteral("light"),
                                          QStringLiteral("dark")})},
        SettingKey{.path = geometryKey,
                   .type = QMetaType::QString,
                   .defaultValue = QString(),
                   .validator = validators::maxLength(1024)},
        SettingKey{
            .path = restoreTabsKey, .type = QMetaType::Bool, .defaultValue = true, .validator = {}},
        SettingKey{.path = tabsKey,
                   .type = QMetaType::QString,
                   .defaultValue = QString(),
                   .validator = validators::maxLength(4096)},
        // 0 = the profile's value (ADR-0052); for the speed 0 = automatic (ADR-0111).
        SettingKey{.path = QStringLiteral("jlink/speedKhz"),
                   .type = QMetaType::Int,
                   .defaultValue = 0,
                   .validator =
                       validators::intRange(device::minSpeedKhz, device::maxSpeedKhz, true)},
        SettingKey{.path = QStringLiteral("rtt/latencyMs"),
                   .type = QMetaType::Int,
                   .defaultValue = 0,
                   .validator =
                       validators::intRange(device::minLatencyMs, device::maxLatencyMs, true)},
        SettingKey{.path = QStringLiteral("rtt/autoReconnect"),
                   .type = QMetaType::Bool,
                   .defaultValue = true,
                   .validator = {}},
        SettingKey{.path = QStringLiteral("rtt/resetOnOpen"),
                   .type = QMetaType::Bool,
                   .defaultValue = false,
                   .validator = {}},
        // 0 = automatic (ADR-0111).
        SettingKey{.path = QStringLiteral("probes/*/speedKhz"),
                   .type = QMetaType::Int,
                   .defaultValue = 0,
                   .validator =
                       validators::intRange(device::minSpeedKhz, device::maxSpeedKhz, true)},
        SettingKey{.path = QStringLiteral("probes/*/latencyMs"),
                   .type = QMetaType::Int,
                   .defaultValue = 0,
                   .validator =
                       validators::intRange(device::minLatencyMs, device::maxLatencyMs, true)},
        SettingKey{.path = QStringLiteral("probes/*/autoReconnect"),
                   .type = QMetaType::Bool,
                   .defaultValue = true,
                   .validator = {}},
        SettingKey{.path = QStringLiteral("probes/*/resetOnOpen"),
                   .type = QMetaType::Bool,
                   .defaultValue = false,
                   .validator = {}},
        // Comma-separated serial numbers, most recent first (≤ 32 × 10 digits).
        SettingKey{.path = QStringLiteral("probes/recent"),
                   .type = QMetaType::QString,
                   .defaultValue = QString(),
                   .validator = validators::maxLength(512)},
    };
    store.registerKeys(keys);
}

void AppViewModel::registerShellShortcuts() {
    struct Shortcut {
        const char* id;
        const char* keys;
        const char* description;
    };
    // ADR-0051. Alternatives share the description (one row in the overlay).
    static constexpr std::array shortcuts{
        Shortcut{.id = "tab.new",
                 .keys = "Ctrl+T",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "New device tab")},
        Shortcut{.id = "tab.close",
                 .keys = "Ctrl+W",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Close the device tab")},
        Shortcut{.id = "tab.next",
                 .keys = "Ctrl+Tab",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Next tab")},
        Shortcut{.id = "tab.next.alt",
                 .keys = "Ctrl+PgDown",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Next tab")},
        Shortcut{.id = "tab.previous",
                 .keys = "Ctrl+Shift+Tab",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Previous tab")},
        Shortcut{.id = "tab.previous.alt",
                 .keys = "Ctrl+PgUp",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Previous tab")},
        Shortcut{.id = "settings.open",
                 .keys = "Ctrl+,",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Settings")},
        Shortcut{.id = "device.toggle",
                 .keys = "Ctrl+Shift+D",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Connect or disconnect")},
        Shortcut{.id = "device.reconnect",
                 .keys = "F4",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Reconnect")},
        Shortcut{.id = "help.shortcuts",
                 .keys = "F1",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Keyboard shortcuts")},
        Shortcut{.id = "help.shortcuts.alt",
                 .keys = "Ctrl+/",
                 .description = QT_TRANSLATE_NOOP("Shortcuts", "Keyboard shortcuts")},
    };
    for (const Shortcut& shortcut : shortcuts) {
        logFailure("shortcut",
                   m_shortcuts->add(QString::fromLatin1(shortcut.id),
                                    QKeySequence(QString::fromLatin1(shortcut.keys)),
                                    ShortcutRegistry::globalScope, shortcut.description));
    }
}

void AppViewModel::createGlobalPages() {
    for (const PageDescriptor& page : m_pages.pages()) {
        if (!page.global) {
            continue;
        }
        QObject* viewModel = page.createViewModel ? page.createViewModel(nullptr) : nullptr;
        if (viewModel != nullptr && viewModel->parent() == nullptr) {
            viewModel->setParent(this); // Qt parent ownership (contract: parent = AppViewModel)
        }
        m_globalViewModels.emplace_back(page.id, viewModel);
    }
}

void AppViewModel::setLanguageManager(LanguageManager* manager) {
    if (m_languageManager) {
        m_languageManager->disconnect(this);
    }
    m_languageManager = manager;
    if (m_languageManager) {
        connect(m_languageManager, &LanguageManager::languageChanged, this,
                &AppViewModel::languageChanged);
        connect(m_languageManager, &LanguageManager::languageChanged, this,
                &AppViewModel::retranslate);
    }
}

QString AppViewModel::version() { return toQString(hio::buildInfo().version); }

void AppViewModel::refreshCrashNotice() {
    const bool crashed =
        !m_options.crashDirectory.empty() && crash::crashedLastRun(m_options.crashDirectory);
    if (crashed != m_crashedLastRun) {
        m_crashedLastRun = crashed;
        emit crashedLastRunChanged();
    }
    if (crashed) {
        qCWarning(lcGui) << "The previous run ended with a crash; crash files are in"
                         << QString::fromStdU16String(m_options.crashDirectory.u16string());
    }
}

void AppViewModel::reportCrash() {
    dismissCrashNotice();
    m_report->show();
}

void AppViewModel::dismissCrashNotice() {
    if (!m_options.crashDirectory.empty()) {
        crash::acknowledge(m_options.crashDirectory);
    }
    if (m_crashedLastRun) {
        m_crashedLastRun = false;
        emit crashedLastRunChanged();
    }
}

QString AppViewModel::buildInfo() {
    const BuildInfo info = hio::buildInfo();
    return QStringLiteral("%1 · Qt %2 · %3")
        .arg(toQString(info.version), toQString(info.qtRuntime), toQString(info.compiler));
}

QString AppViewModel::language() const {
    return m_languageManager ? m_languageManager->language() : QStringLiteral("system");
}

QString AppViewModel::effectiveLanguage() const {
    return m_languageManager ? m_languageManager->effectiveLanguage() : QStringLiteral("en");
}

void AppViewModel::setLanguage(const QString& code) {
    if (!m_languageManager) {
        qCWarning(lcGui) << "setLanguage" << code << "ignored: no LanguageManager";
        return;
    }
    if (const Status status = m_languageManager->apply(code); !status) {
        qCWarning(lcGui) << "setLanguage" << code << "failed:" << toQString(status.error().message)
                         << toQString(status.error().detail);
    }
}

QString AppViewModel::theme() const { return m_settings->value(themeKey).toString(); }

void AppViewModel::setTheme(const QString& theme) {
    if (theme == this->theme()) {
        return;
    }
    if (const Status status = m_settings->setValue(themeKey, theme); !status) {
        qCWarning(lcGui) << "setTheme" << theme << "failed:" << toQString(status.error().detail);
        themeChanged(); // signal: QML shows the stored value again
    }
    // valueChanged -> themeChanged
}

void AppViewModel::setCurrentTab(int index) {
    if (index < 0 || index >= m_tabs->rowTotal()) {
        return;
    }
    if (index < m_tabs->count()) {
        m_lastDeviceTab = index;
    }
    if (index != m_currentTab) {
        const QString page = globalPage();
        m_currentTab = index;
        currentTabChanged(); // signal
        if (page != globalPage()) {
            globalPageChanged(); // signal: the Settings tab became current or was left
        }
        saveTabs();
    }
}

QString AppViewModel::globalPage() const {
    // 0021 (ADR-0172): the Settings tab hosts the global page.
    return m_tabs->settingsOpen() && m_currentTab == m_tabs->settingsRow()
               ? QStringLiteral("settings")
               : QString();
}

DeviceTabViewModel* AppViewModel::currentTabViewModel() const { return m_tabs->at(m_currentTab); }

void AppViewModel::setWindowWidth(int width) {
    if (width == m_windowWidth) {
        return;
    }
    const bool compact = railCompact();
    m_windowWidth = width;
    if (compact != railCompact()) {
        railCompactChanged(); // signal
    }
}

void AppViewModel::setShortcutOverlayVisible(bool visible) {
    if (visible == m_shortcutOverlay) {
        return;
    }
    m_shortcutOverlay = visible;
    shortcutOverlayVisibleChanged(); // signal
}

QStringList AppViewModel::toasts() const {
    QStringList texts;
    for (const Toast& toast : m_visibleToasts) {
        texts.append(toast.text);
    }
    return texts;
}

jlink::LocatorOptions AppViewModel::locatorOptions() const {
    return jlink::defaultLocatorOptions(std::nullopt, m_settings->settings());
}

void AppViewModel::updateLocatorOptions() { m_scanner->setLocatorOptions(locatorOptions()); }

// ---- tabs -------------------------------------------------------------------------------------

std::unique_ptr<DeviceTabViewModel> AppViewModel::makeTab() {
    TabContext context;
    context.registry = m_registry.get();
    context.settings = m_settings;
    context.pages = &m_pages;
    context.scanner = m_scanner.get();
    context.locatorOptions = [this] { return locatorOptions(); };
    context.tabUsingProbe = [this](std::uint32_t serial, const DeviceTabViewModel* self) {
        return tabUsingProbe(serial, self);
    };
    context.showToast = [this](const QString& text) { showToast(text); };
    context.openWizard = [this](const QString& wizardId, DeviceTabViewModel* tab) {
        (void)m_launcher->openForTab(wizardId, tab);
    };
    return std::make_unique<DeviceTabViewModel>(std::move(context), nullptr);
}

DeviceTabViewModel* AppViewModel::addTab(std::unique_ptr<DeviceTabViewModel> tab) {
    DeviceTabViewModel* raw = tab.get();
    const bool settingsCurrent = m_tabs->settingsOpen() && m_currentTab == m_tabs->settingsRow();
    if (!m_tabs->append(std::move(tab))) {
        return nullptr;
    }
    if (settingsCurrent) {
        // 0021 review F2: the new device row was inserted before the Settings row, which stays
        // current (its index moved); a following setCurrentTab() then notifies the change.
        m_currentTab = m_tabs->settingsRow();
    }
    connect(raw, &DeviceTabViewModel::configurationChanged, this, [this] {
        saveTabs();
        // Other pickers show "In use (tab N)" for this probe now.
        for (int i = 0; i < m_tabs->count(); ++i) {
            m_tabs->at(i)->picker()->updateFromScanner();
        }
    });
    connect(raw, &DeviceTabViewModel::tabActivationRequested, this, &AppViewModel::setCurrentTab);
    connect(raw, &DeviceTabViewModel::closeReady, this, [this, raw] {
        if (const int index = m_tabs->indexOf(raw); index >= 0) {
            removeTab(index);
        }
    });
    connect(raw, &DeviceTabViewModel::busyChanged, this, [this] { checkQuit(); });
    return raw;
}

void AppViewModel::newTab() {
    if (!m_tabs->canAdd()) {
        return;
    }
    if (addTab(makeTab()) != nullptr) {
        setCurrentTab(m_tabs->count() - 1);
    }
}

void AppViewModel::closeTab(int index, bool force) {
    if (index >= 0 && index == m_tabs->settingsRow()) {
        closeSettings(); // 0021 (ADR-0172)
        return;
    }
    DeviceTabViewModel* tab = m_tabs->at(index);
    if (tab == nullptr) {
        return;
    }
    if (tab->isBusy()) {
        if (!force) {
            // The tab itself, not its index: tabs may move while the dialog is open (S6).
            closeConfirmationNeeded(tab, tab->busyConfirmText()); // signal
        } else {
            tab->cancelBusy(true); // closes on closeReady
        }
        return;
    }
    removeTab(index);
}

void AppViewModel::closeTabObject(DeviceTabViewModel* tab, bool force) {
    const int index = m_tabs->indexOf(tab);
    if (index >= 0) {
        closeTab(index, force);
    }
}

void AppViewModel::removeTab(int index) {
    std::unique_ptr<DeviceTabViewModel> tab = m_tabs->take(index);
    if (!tab) {
        return;
    }
    tab->disconnect(this);
    // The wizard working on this tab closes first, while the tab and its session still exist
    // (review V2): the wizard holds the tab as QPointer, but its operation runs on the session.
    m_wizard->tabClosing(tab.get());
    tab->shutdown();
    // Deleted later, after QML has destroyed its delegates (they were scheduled for deletion by
    // the row removal, before this). The call may also come from the tab's own signal or from a
    // QML handler inside its delegate. Qt parent ownership until then (also when the event loop
    // does not run again).
    tab->setParent(this);
    tab.release()->deleteLater();

    const bool settingsCurrent =
        m_tabs->settingsOpen() && m_currentTab == m_tabs->settingsRow() + 1;
    if (m_tabs->count() == 0) {
        (void)addTab(makeTab()); // the last tab leaves one empty Choose device tab
    }
    int current = m_currentTab;
    if (index < current) {
        --current;
    }
    if (index < m_lastDeviceTab) {
        --m_lastDeviceTab;
    }
    m_lastDeviceTab = std::clamp(m_lastDeviceTab, 0, m_tabs->count() - 1);
    current = settingsCurrent ? m_tabs->settingsRow() : std::clamp(current, 0, m_tabs->count() - 1);
    m_currentTab = -1; // force the notification: another tab is at the index now
    setCurrentTab(current);
    saveTabs();
    for (int i = 0; i < m_tabs->count(); ++i) {
        m_tabs->at(i)->picker()->updateFromScanner();
    }
}

void AppViewModel::nextTab() {
    // 0021 (ADR-0172): the Settings tab is cycled like any tab.
    const int rows = m_tabs->rowTotal();
    if (rows > 1) {
        setCurrentTab((m_currentTab + 1) % rows);
    }
}

void AppViewModel::previousTab() {
    const int rows = m_tabs->rowTotal();
    if (rows > 1) {
        setCurrentTab((m_currentTab + rows - 1) % rows);
    }
}

void AppViewModel::showPage(const QString& pageId) {
    if (!globalPage().isEmpty()) {
        // A per-tab page from the Settings tab: back to the last device tab.
        setCurrentTab(std::clamp(m_lastDeviceTab, 0, m_tabs->count() - 1));
    }
    if (DeviceTabViewModel* tab = currentTabViewModel(); tab != nullptr && !tab->choosing()) {
        tab->setCurrentPage(pageId);
    }
}

void AppViewModel::openGlobalPage(const QString& pageId) {
    // 0021 (ADR-0172): the only global page, Settings, is a tab now.
    const PageDescriptor* page = m_pages.find(pageId);
    if (page == nullptr || !page->global) {
        return;
    }
    openSettings();
}

void AppViewModel::closeGlobalPage() { closeSettings(); }

void AppViewModel::openSettings(const QString& sectionId) {
    m_tabs->openSettings();
    setCurrentTab(m_tabs->settingsRow());
    m_settingsSection = sectionId;
    settingsSectionRequested(sectionId); // signal: the page scrolls to the section
}

void AppViewModel::closeSettings() {
    if (!m_tabs->settingsOpen()) {
        return;
    }
    const bool wasCurrent = m_currentTab == m_tabs->settingsRow();
    m_tabs->closeSettings();
    m_settingsSection.clear();
    if (wasCurrent) {
        m_currentTab = -1; // force the notification
        setCurrentTab(std::clamp(m_lastDeviceTab, 0, m_tabs->count() - 1));
        globalPageChanged(); // signal
    }
}

QObject* AppViewModel::globalPageViewModel(const QString& pageId) const {
    const auto it = std::ranges::find_if(
        m_globalViewModels, [&pageId](const auto& entry) { return entry.first == pageId; });
    return it == m_globalViewModels.end() ? nullptr : it->second;
}

void AppViewModel::toggleConnection() const {
    if (DeviceTabViewModel* tab = currentTabViewModel(); tab != nullptr && !tab->choosing()) {
        tab->connection()->toggleConnection();
    }
}

void AppViewModel::reconnect() const {
    if (DeviceTabViewModel* tab = currentTabViewModel(); tab != nullptr && !tab->choosing()) {
        tab->reconnect();
    }
}

int AppViewModel::tabUsingProbe(std::uint32_t serial, const DeviceTabViewModel* self) const {
    for (int i = 0; i < m_tabs->count(); ++i) {
        const DeviceTabViewModel* tab = m_tabs->at(i);
        if (tab != self && !tab->choosing() && tab->probeSerial() == serial) {
            return i;
        }
    }
    return -1;
}

// ---- restore and save (ADR-0061) --------------------------------------------------------------

void AppViewModel::restoreTabs() {
    m_restoring = true;
    struct Entry {
        QString profile;
        QString target;
        std::uint32_t serial = 0;
    };
    std::vector<Entry> entries;
    int current = 0;
    bool valid = true;
    const QString stored = m_settings->value(tabsKey).toString();
    if (m_settings->value(restoreTabsKey).toBool() && !stored.isEmpty()) {
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(stored.toUtf8(), &parseError);
        const QJsonArray tabs = document.object().value(QStringLiteral("tabs")).toArray();
        valid = parseError.error == QJsonParseError::NoError && document.isObject() &&
                std::cmp_less_equal(tabs.size(), DeviceTabListModel::maxTabs);
        current = document.object().value(QStringLiteral("current")).toInt(0);
        for (const auto value : tabs) {
            if (!valid) {
                break;
            }
            const QJsonObject object = value.toObject();
            const double serial = object.value(QStringLiteral("sn")).toDouble(0);
            Entry entry{.profile = object.value(QStringLiteral("profile")).toString(),
                        .target = object.value(QStringLiteral("target")).toString(),
                        .serial = 0};
            const bool duplicate = std::ranges::any_of(entries, [serial](const Entry& known) {
                return known.serial == static_cast<std::uint32_t>(serial);
            });
            if (serial < 1 || serial > 4294967295.0 || duplicate) {
                valid = false;
                break;
            }
            entry.serial = static_cast<std::uint32_t>(serial);
            entries.push_back(std::move(entry));
        }
    }

    if (valid) {
        for (const Entry& entry : entries) {
            std::unique_ptr<DeviceTabViewModel> tab = makeTab();
            if (!tab->configure(entry.profile, entry.target, entry.serial)) {
                valid = false;
                break;
            }
            (void)addTab(std::move(tab));
        }
    }
    if (!valid) {
        qCWarning(lcGui) << "The saved tab list is invalid and was ignored:" << stored;
        while (m_tabs->count() > 0) {
            m_tabs->take(0)->shutdown();
        }
        current = 0;
    }
    if (m_tabs->count() == 0) {
        (void)addTab(makeTab()); // first start: one Choose device tab
    }
    m_currentTab = std::clamp(current, 0, m_tabs->count() - 1);
    m_lastDeviceTab = m_currentTab;
    m_restoring = false;
}

void AppViewModel::saveTabs() {
    if (m_restoring || m_settings->readOnly()) {
        return;
    }
    QJsonArray tabs;
    int current = 0;
    for (int i = 0; i < m_tabs->count(); ++i) {
        const DeviceTabViewModel* tab = m_tabs->at(i);
        const std::optional<std::uint32_t> serial = tab->probeSerial();
        if (tab->choosing() || !serial) {
            continue;
        }
        if (i == m_currentTab || (m_currentTab == m_tabs->settingsRow() && i == m_lastDeviceTab)) {
            current = static_cast<int>(tabs.size());
        }
        tabs.append(QJsonObject{{QStringLiteral("profile"), tab->profileId()},
                                {QStringLiteral("target"), tab->consoleTargetId()},
                                {QStringLiteral("sn"), static_cast<double>(*serial)}});
    }
    const QJsonObject root{{QStringLiteral("current"), current}, {QStringLiteral("tabs"), tabs}};
    const QString text = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (text != m_settings->value(tabsKey).toString()) {
        logFailure("ui/tabs", m_settings->setValue(tabsKey, text));
    }
}

void AppViewModel::saveState() {
    saveTabs();
    saveGeometry();
}

void AppViewModel::attachWindow(QWindow* window) {
    m_window = window;
    if (window == nullptr) {
        return;
    }
    const QByteArray stored =
        QByteArray::fromBase64(m_settings->value(geometryKey).toString().toLatin1());
    const QJsonObject object = QJsonDocument::fromJson(stored).object();
    const QRect rect(
        object.value(QStringLiteral("x")).toInt(), object.value(QStringLiteral("y")).toInt(),
        object.value(QStringLiteral("w")).toInt(), object.value(QStringLiteral("h")).toInt());
    if (object.value(QStringLiteral("v")).toInt() != 1 || rect.width() < minWindowWidth ||
        rect.height() < minWindowHeight) {
        return; // the QML defaults (1280 × 800)
    }
    // Only when the window would be visible on a screen that is connected now.
    const bool onScreen = std::ranges::any_of(QGuiApplication::screens(), [&rect](QScreen* screen) {
        return screen->availableGeometry().intersects(rect.adjusted(0, 0, 0, -rect.height() + 40));
    });
    if (onScreen) {
        window->setGeometry(rect);
    } else {
        window->resize(rect.size());
    }
    if (object.value(QStringLiteral("maximized")).toBool()) {
        window->setWindowStates(Qt::WindowMaximized);
    }
}

void AppViewModel::saveGeometry() {
    if (!m_window || m_settings->readOnly()) {
        return;
    }
    const bool maximized = (m_window->windowStates() & Qt::WindowMaximized) != 0;
    // A maximized window keeps the previous normal geometry when one is stored.
    QRect rect = m_window->geometry();
    if (maximized) {
        const QJsonObject previous =
            QJsonDocument::fromJson(
                QByteArray::fromBase64(m_settings->value(geometryKey).toString().toLatin1()))
                .object();
        if (previous.value(QStringLiteral("v")).toInt() == 1) {
            rect = QRect(previous.value(QStringLiteral("x")).toInt(),
                         previous.value(QStringLiteral("y")).toInt(),
                         previous.value(QStringLiteral("w")).toInt(),
                         previous.value(QStringLiteral("h")).toInt());
        }
    }
    const QJsonObject object{{QStringLiteral("v"), 1},
                             {QStringLiteral("x"), rect.x()},
                             {QStringLiteral("y"), rect.y()},
                             {QStringLiteral("w"), std::max(rect.width(), minWindowWidth)},
                             {QStringLiteral("h"), std::max(rect.height(), minWindowHeight)},
                             {QStringLiteral("maximized"), maximized}};
    const QString text =
        QString::fromLatin1(QJsonDocument(object).toJson(QJsonDocument::Compact).toBase64());
    logFailure("ui/windowGeometry", m_settings->setValue(geometryKey, text));
}

void AppViewModel::activateWindow() {
    if (!m_window) {
        return;
    }
    if ((m_window->windowStates() & Qt::WindowMinimized) != 0) {
        m_window->setWindowStates(m_window->windowStates() & ~Qt::WindowMinimized);
    }
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}

// ---- quit -------------------------------------------------------------------------------------

bool AppViewModel::canQuit() {
    for (int i = 0; i < m_tabs->count(); ++i) {
        const DeviceTabViewModel* tab = m_tabs->at(i);
        if (tab->isBusy()) {
            quitConfirmationNeeded(tab->busyConfirmText()); // signal
            return false;
        }
    }
    saveState();
    return true;
}

void AppViewModel::cancelOperationsAndQuit() {
    m_quitPending = true;
    for (int i = 0; i < m_tabs->count(); ++i) {
        m_tabs->at(i)->cancelBusy(false);
    }
    checkQuit();
}

void AppViewModel::checkQuit() {
    if (!m_quitPending) {
        return;
    }
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->at(i)->isBusy()) {
            return;
        }
    }
    m_quitPending = false;
    saveState();
    quitRequested(); // signal
}

// ---- toasts, misc -----------------------------------------------------------------------------

void AppViewModel::showToast(const QString& text) {
    if (text.isEmpty()) {
        return;
    }
    if (m_visibleToasts.size() >= maxVisibleToasts) {
        if (m_queuedToasts.size() >= maxQueuedToasts) {
            m_queuedToasts.erase(m_queuedToasts.begin()); // bounded: drop the oldest
        }
        m_queuedToasts.push_back(text);
        return;
    }
    const std::uint64_t id = m_nextToastId++;
    m_visibleToasts.push_back(Toast{.id = id, .text = text});
    QTimer::singleShot(toastDuration, this, [this, id] { expireToast(id); });
    toastsChanged(); // signal
}

void AppViewModel::expireToast(std::uint64_t id) {
    std::erase_if(m_visibleToasts, [id](const Toast& toast) { return toast.id == id; });
    toastsChanged(); // signal
    if (!m_queuedToasts.empty()) {
        const QString next = m_queuedToasts.front();
        m_queuedToasts.erase(m_queuedToasts.begin());
        showToast(next);
    }
}

int AppViewModel::stepValue(int value, int step, int direction, int minimum, int maximum) {
    if (step <= 0 || direction == 0) {
        return std::clamp(value, minimum, maximum);
    }
    // Floor / ceiling division that also works for negative values.
    const auto floorDiv = [](int a, int b) { return (a / b) - ((a % b != 0 && a < 0) ? 1 : 0); };
    const auto ceilDiv = [](int a, int b) { return (a / b) + ((a % b != 0 && a > 0) ? 1 : 0); };
    const long long next = direction > 0
                               ? (static_cast<long long>(floorDiv(value, step)) + 1) * step
                               : (static_cast<long long>(ceilDiv(value, step)) - 1) * step;
    return static_cast<int>(std::clamp<long long>(next, minimum, maximum));
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): a QML invokable
void AppViewModel::copyToClipboard(const QString& text) {
    if (QClipboard* clipboard = QGuiApplication::clipboard(); clipboard != nullptr) {
        clipboard->setText(text);
    }
}

void AppViewModel::onScan(const ProbeScan& scan) {
    const QString path = scan.libraryPath.empty() ? QString() : toQString(toUtf8(scan.libraryPath));
    const QString version = scan.libraryVersion != 0
                                ? toQString(jlink::formatLibraryVersion(scan.libraryVersion))
                                : QString();
    if (path != m_jlinkLibraryPath || version != m_jlinkLibraryVersion) {
        m_jlinkLibraryPath = path;
        m_jlinkLibraryVersion = version;
        jlinkLibraryChanged(); // signal
    }
}

diag::EnvironmentContext AppViewModel::diagEnvironment() const {
    diag::EnvironmentContext context;
    context.jlink = locatorOptions();
    context.nrfutil = nrfutil::defaultLocatorOptions(std::nullopt, m_settings->settings());
    context.settingsDir = Settings::defaultFilePath().parent_path();
    context.logDir = log::defaultLogDirectory();
    context.instanceRoot = m_options.registry.instanceRoot;
    context.lockDirectory = m_options.registry.lockDirectory;
    return context;
}

QString AppViewModel::documentationUrl() {
    return QStringLiteral("https://docs.hardwario.com/chester/platform-connectivity/"
                          "hardwario-monitor/");
}

void AppViewModel::openDocumentation() { QDesktopServices::openUrl(QUrl(documentationUrl())); }

QJsonArray AppViewModel::tabsJson() const {
    QJsonArray tabs;
    for (int i = 0; i < m_tabs->count(); ++i) {
        const DeviceTabViewModel* tab = m_tabs->at(i);
        if (tab == nullptr || tab->choosing()) {
            continue;
        }
        QJsonObject json;
        json.insert(QStringLiteral("profile"), tab->profileId());
        json.insert(QStringLiteral("target"), tab->consoleTargetId());
        json.insert(QStringLiteral("serialNumber"),
                    static_cast<qint64>(tab->probeSerial().value_or(0)));
        json.insert(QStringLiteral("state"), tab->connection()->stateKind());
        tabs.append(json);
    }
    return tabs;
}

void AppViewModel::onHandOff(const QString& action, DeviceTabViewModel* tab) {
    qCInfo(lcGui).noquote() << "Wizard hand-off:" << action;
    if (action.startsWith(QStringLiteral("open-settings"))) {
        // "open-settings[:<section>]" (0021: the Settings tab scrolls to the section).
        const qsizetype colon = action.indexOf(QLatin1Char(':'));
        openSettings(colon >= 0 ? action.mid(colon + 1) : QString());
        return;
    }
    if (tab == nullptr) {
        return;
    }
    if (const int index = m_tabs->indexOf(tab); index >= 0) {
        setCurrentTab(index);
    }
    if (action == QStringLiteral("open-console")) {
        if (tab->state() == DeviceTabViewModel::Disconnected) {
            tab->connectDevice(); // also promotes a wizard's probe-only session
        }
        showPage(QStringLiteral("console"));
    } else if (action == QStringLiteral("open-flash-page")) {
        showPage(QStringLiteral("flash"));
    }
}

void AppViewModel::retranslate() {
    m_pageList->retranslate();
    m_tabs->retranslate();
    m_shortcuts->retranslate();
    for (int i = 0; i < m_tabs->count(); ++i) {
        m_tabs->at(i)->retranslate();
    }
}

} // namespace hio::gui
