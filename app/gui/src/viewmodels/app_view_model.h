#pragma once

// View model of the application window (ADR-0003: no logic in QML;
// plan/features/0004-m3-gui-shell.md).
//
// Owns the GUI's application state: the SettingsStore, the PageRegistry (pages and settings
// sections), the ShortcutRegistry, the SessionRegistry, the ProbeScanner worker, the device tabs
// (≤ 8, restored Disconnected from ui/tabs, ADR-0061), the global page view models, the toasts
// (≤ 3 visible) and the theme. Language switching is forwarded to the LanguageManager.
//
// Ownership: created by main() (and the test runners) on the stack with the application object as
// QObject parent, so that the QML singleton factory create() finds it as a direct child of the
// application; QML never owns it. The destructor closes the tabs (and so their sessions) before
// the registries and the scanner go away. GUI thread only.

#include "hio/core/settings.h"
#include "hio/device/session_registry.h"
#include "hio/hw/jlink/library_locator.h"
#include "settings_store.h"
#include "viewmodels/device_tab_list_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/page_list_model.h"
#include "viewmodels/page_registry.h"
#include "viewmodels/shortcut_registry.h"
#include "wizard/report_view_model.h"
#include "wizard/wizard_launcher.h"
#include "wizard/wizard_registry.h"
#include "wizard/wizard_view_model.h"
#include "workers/diag_worker.h"
#include "workers/probe_scanner.h"

#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QJSEngine;
class QQmlEngine;
class QWindow;

namespace hio::gui {

class DeviceTabViewModel;
class LanguageManager;
class AppViewModel;

struct AppOptions {
    std::optional<Settings> settings; // empty: in-memory settings for this run
    device::RegistryOptions registry; // instance, lock and console directories
    std::chrono::milliseconds scanInterval = ProbeScanner::defaultInterval;
    // Registers the pages, settings sections, setting keys and page shortcuts of the GUI plans
    // (0004's placeholders, 0005, 0006), before the first tab exists.
    std::function<void(AppViewModel&)> registerExtensions;
};

class AppViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString buildInfo READ buildInfo CONSTANT)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
    Q_PROPERTY(QVariantList availableLanguages READ availableLanguages CONSTANT)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged) // system|light|dark
    Q_PROPERTY(bool reducedMotion READ reducedMotion CONSTANT)
    Q_PROPERTY(hio::gui::DeviceTabListModel* tabs READ tabs CONSTANT)
    Q_PROPERTY(int currentTab READ currentTab WRITE setCurrentTab NOTIFY currentTabChanged)
    Q_PROPERTY(hio::gui::DeviceTabViewModel* currentTabViewModel READ currentTabViewModel NOTIFY
                   currentTabChanged)
    Q_PROPERTY(int maxTabs READ maxTabs CONSTANT)
    // Pages for the rail and the page hosts, in order; titles translated (review S9).
    Q_PROPERTY(hio::gui::PageListModel* pages READ pages CONSTANT)
    Q_PROPERTY(QString globalPage READ globalPage NOTIFY globalPageChanged) // "" = the tab's page
    Q_PROPERTY(int windowWidth READ windowWidth WRITE setWindowWidth NOTIFY railCompactChanged)
    Q_PROPERTY(bool railCompact READ railCompact NOTIFY railCompactChanged) // width < 1100
    Q_PROPERTY(bool shortcutOverlayVisible READ shortcutOverlayVisible WRITE
                   setShortcutOverlayVisible NOTIFY shortcutOverlayVisibleChanged)
    Q_PROPERTY(hio::gui::ShortcutRegistry* shortcuts READ shortcuts CONSTANT)
    Q_PROPERTY(hio::gui::SettingsStore* settings READ settingsStore CONSTANT)
    Q_PROPERTY(QStringList toasts READ toasts NOTIFY toastsChanged)
    Q_PROPERTY(QString jlinkLibraryPath READ jlinkLibraryPath NOTIFY jlinkLibraryChanged)
    Q_PROPERTY(QString jlinkLibraryVersion READ jlinkLibraryVersion NOTIFY jlinkLibraryChanged)
    // M4 (plan/features/0007): the open wizard, the launcher and the diagnostic report dialog.
    Q_PROPERTY(hio::gui::WizardViewModel* wizard READ wizard CONSTANT)
    Q_PROPERTY(hio::gui::WizardLauncher* launcher READ launcher CONSTANT)
    Q_PROPERTY(hio::gui::ReportViewModel* report READ report CONSTANT)
    Q_PROPERTY(QString documentationUrl READ documentationUrl CONSTANT)

public:
    static constexpr int railBreakpoint = 1100; // ADR-0061
    static constexpr std::size_t maxVisibleToasts = 3;
    static constexpr std::size_t maxQueuedToasts = 16;
    static constexpr std::chrono::milliseconds toastDuration{4000};

    explicit AppViewModel(AppOptions options, QObject* parent = nullptr);
    ~AppViewModel() override;
    AppViewModel(const AppViewModel&) = delete;
    AppViewModel& operator=(const AppViewModel&) = delete;
    AppViewModel(AppViewModel&&) = delete;
    AppViewModel& operator=(AppViewModel&&) = delete;

    // QML singleton factory: the instance created by main() (a direct child of the application).
    // Without one (should not happen), a default instance owned by the engine is created.
    static AppViewModel* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    // The setting keys of the shell (ui/*, jlink/*, rtt/*, probes/*); also used by main() for the
    // "already running" dialog, which needs the language and the theme before this object exists.
    static void registerShellKeys(SettingsStore& store);

    void setLanguageManager(LanguageManager* manager);
    // Restores the window geometry (ui/windowGeometry); saveState() stores it.
    void attachWindow(QWindow* window);
    // Brings the window to the front (second instance, ADR-0058).
    void activateWindow();

    [[nodiscard]] static QString version();
    [[nodiscard]] static QString buildInfo();
    [[nodiscard]] QString language() const;
    [[nodiscard]] QVariantList availableLanguages() const { return m_availableLanguages; }
    Q_INVOKABLE void setLanguage(const QString& code);
    [[nodiscard]] QString theme() const;
    void setTheme(const QString& theme);
    [[nodiscard]] bool reducedMotion() const noexcept { return m_reducedMotion; }

    [[nodiscard]] DeviceTabListModel* tabs() const { return m_tabs; }
    [[nodiscard]] int currentTab() const noexcept { return m_currentTab; }
    void setCurrentTab(int index);
    [[nodiscard]] DeviceTabViewModel* currentTabViewModel() const;
    [[nodiscard]] static int maxTabs() { return static_cast<int>(DeviceTabListModel::maxTabs); }
    [[nodiscard]] PageListModel* pages() const { return m_pageList; }
    [[nodiscard]] QString globalPage() const { return m_globalPage; }
    [[nodiscard]] int windowWidth() const noexcept { return m_windowWidth; }
    void setWindowWidth(int width);
    [[nodiscard]] bool railCompact() const noexcept { return m_windowWidth < railBreakpoint; }
    [[nodiscard]] bool shortcutOverlayVisible() const noexcept { return m_shortcutOverlay; }
    void setShortcutOverlayVisible(bool visible);
    [[nodiscard]] ShortcutRegistry* shortcuts() const { return m_shortcuts; }
    [[nodiscard]] SettingsStore* settingsStore() const { return m_settings; }
    [[nodiscard]] QStringList toasts() const;
    [[nodiscard]] QString jlinkLibraryPath() const { return m_jlinkLibraryPath; }
    [[nodiscard]] QString jlinkLibraryVersion() const { return m_jlinkLibraryVersion; }
    [[nodiscard]] WizardViewModel* wizard() const { return m_wizard; }
    [[nodiscard]] WizardLauncher* launcher() const { return m_launcher; }
    [[nodiscard]] ReportViewModel* report() const { return m_report; }
    [[nodiscard]] static QString documentationUrl();

    // --- registries (extension registration and tests) ---
    [[nodiscard]] PageRegistry& pageRegistry() { return m_pages; }
    [[nodiscard]] device::SessionRegistry& sessionRegistry() { return *m_registry; }
    [[nodiscard]] ProbeScanner& probeScanner() { return *m_scanner; }
    // M4: the wizard plans (0008-0010) register their wizards here (registerExtensions).
    [[nodiscard]] WizardRegistry& wizardRegistry() { return m_wizardRegistry; }
    [[nodiscard]] DiagWorker& diagWorker() { return *m_diagWorker; }
    // The environment of the checks (settings, overrides; GUI thread).
    [[nodiscard]] diag::EnvironmentContext diagEnvironment() const;
    // Environment, the jlink/libraryPath setting and the search paths of this OS.
    [[nodiscard]] jlink::LocatorOptions locatorOptions() const;

    // --- tabs ---
    Q_INVOKABLE void newTab();
    // Closes a tab. While a page of it is busy and !force: closeConfirmationNeeded. With force:
    // cancels the operation and closes when the page has finished. The last tab is replaced by an
    // empty Choose device tab.
    Q_INVOKABLE void closeTab(int index, bool force = false);
    // The same for a tab object (the close confirmation keeps the tab, not its index; review S6).
    // Ignored when the tab is gone.
    Q_INVOKABLE void closeTabObject(hio::gui::DeviceTabViewModel* tab, bool force);
    Q_INVOKABLE void nextTab();
    Q_INVOKABLE void previousTab();
    // Selects a per-tab page of the current tab (and leaves a global page).
    Q_INVOKABLE void showPage(const QString& pageId);
    Q_INVOKABLE void openGlobalPage(const QString& pageId);
    Q_INVOKABLE void closeGlobalPage();
    // The view model of a global page.
    Q_INVOKABLE [[nodiscard]] QObject* globalPageViewModel(const QString& pageId) const;
    // Device bar shortcuts on the current tab (Ctrl+Shift+D, F4).
    Q_INVOKABLE void toggleConnection() const;
    Q_INVOKABLE void reconnect() const;

    // --- quit (ADR-0043) ---
    // false + quitConfirmationNeeded(text) while a page is busy.
    Q_INVOKABLE bool canQuit();
    // [Cancel and quit]: cancels the operations; quitRequested when all pages have finished.
    Q_INVOKABLE void cancelOperationsAndQuit();
    // Saves the tab list (ui/tabs) and the window geometry (ui/windowGeometry).
    Q_INVOKABLE void saveState();

    // --- misc ---
    Q_INVOKABLE void showToast(const QString& text);
    Q_INVOKABLE void copyToClipboard(const QString& text);
    // Help → Documentation (opens the browser).
    Q_INVOKABLE static void openDocumentation();
    // One spin-box step on the grid of `step` (review S15): up from 1 → 10, from 10 → 20; down
    // from 15 → 10; clamped to [minimum, maximum]. direction: +1 / -1.
    Q_INVOKABLE [[nodiscard]] static int stepValue(int value, int step, int direction, int minimum,
                                                   int maximum);

signals:
    void languageChanged();
    void themeChanged();
    void currentTabChanged();
    void globalPageChanged();
    void railCompactChanged();
    void shortcutOverlayVisibleChanged();
    void toastsChanged();
    void jlinkLibraryChanged();
    void closeConfirmationNeeded(hio::gui::DeviceTabViewModel* tab, const QString& text);
    void quitConfirmationNeeded(const QString& text);
    void quitRequested();

private:
    struct Toast {
        std::uint64_t id = 0;
        QString text;
    };

    void registerShellShortcuts();
    void createGlobalPages();
    void restoreTabs();
    [[nodiscard]] std::unique_ptr<DeviceTabViewModel> makeTab();
    DeviceTabViewModel* addTab(std::unique_ptr<DeviceTabViewModel> tab);
    void removeTab(int index);
    void saveTabs();
    void saveGeometry();
    void retranslate();
    void onScan(const ProbeScan& scan);
    void updateLocatorOptions();
    void expireToast(std::uint64_t id);
    void checkQuit();
    [[nodiscard]] int tabUsingProbe(std::uint32_t serial, const DeviceTabViewModel* self) const;
    // A wizard's hand-off (plan/features/0007): "open-console", "open-flash-page",
    // "open-settings[:<section>]".
    void onHandOff(const QString& action, DeviceTabViewModel* tab);
    [[nodiscard]] QJsonArray tabsJson() const;

    AppOptions m_options;
    SettingsStore* m_settings = nullptr;     // child
    ShortcutRegistry* m_shortcuts = nullptr; // child
    PageRegistry m_pages;
    PageListModel* m_pageList = nullptr; // child
    // Destroyed after the tabs (the destructor closes them) and in reverse order: the scanner's
    // worker is joined before the registry goes away.
    std::unique_ptr<device::SessionRegistry> m_registry;
    std::unique_ptr<ProbeScanner> m_scanner;
    // M4: filled by registerExtensions; the worker is joined before the registry goes away.
    WizardRegistry m_wizardRegistry;
    std::unique_ptr<DiagWorker> m_diagWorker;
    WizardViewModel* m_wizard = nullptr;  // child; deleted first by the destructor
    WizardLauncher* m_launcher = nullptr; // child
    ReportViewModel* m_report = nullptr;  // child
    DeviceTabListModel* m_tabs = nullptr; // child
    std::vector<std::pair<QString, QObject*>> m_globalViewModels; // children
    QPointer<LanguageManager> m_languageManager;                  // non-owning, owned by main()
    QPointer<QWindow> m_window; // non-owning, owned by the QML engine
    QVariantList m_availableLanguages;
    int m_currentTab = 0;
    QString m_globalPage;
    int m_windowWidth = 1280;
    bool m_shortcutOverlay = false;
    bool m_reducedMotion = false;
    bool m_quitPending = false;
    bool m_restoring = false;
    std::vector<Toast> m_visibleToasts;
    std::vector<QString> m_queuedToasts;
    std::uint64_t m_nextToastId = 1;
    QString m_jlinkLibraryPath;
    QString m_jlinkLibraryVersion;
};

} // namespace hio::gui
