#include "pages/flash_settings_pages.h"

#include "gui_log.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/session_registry.h"
#include "hio/hw/nrfutil/nrfutil.h"
#include "pages/firmware_services.h"
#include "settings_store.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/flash_view_model.h"
#include "viewmodels/page_registry.h"
#include "viewmodels/settings_view_model.h"
#include "viewmodels/shortcut_registry.h"

#include <QKeySequence>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtCore/qttranslation.h>

#include <optional>

namespace hio::gui {

namespace {

QUrl qmlUrl(const char* file) {
    return {QStringLiteral("qrc:/qt/qml/HardwarioMonitor/qml/") + QString::fromLatin1(file)};
}

void logFailure(const char* what, const Status& status) {
    if (!status) {
        qCWarning(lcGui) << "Flash/Settings registration:" << what
                         << toQString(status.error().detail);
    }
}

} // namespace

void registerFlashAndSettingsPages(AppViewModel& app) {
    SettingsStore* settings = app.settingsStore();
    FirmwareServices::registerKeys(*settings);

    // Qt parent ownership: AppViewModel (deleted after the tabs and the global page view models).
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto* services = new FirmwareServices(
        FirmwareServices::Options{
            .settings = settings,
            .instanceRoot = app.sessionRegistry().options().instanceRoot,
            .cacheDirectory = {},
            .jlinkOptions = [&app] { return app.locatorOptions(); },
            .nrfUtilOptions =
                [settings] {
                    return nrfutil::defaultLocatorOptions(std::nullopt, settings->settings());
                },
            .isFlashPageVisible =
                [&app](const DeviceTabViewModel* tab) {
                    return app.globalPage().isEmpty() && app.currentTabViewModel() == tab &&
                           tab->currentPage() == FlashViewModel::pageId;
                }},
        &app);
    QObject::connect(&app, &AppViewModel::languageChanged, services,
                     &FirmwareServices::retranslate);

    PageRegistry& pages = app.pageRegistry();
    pages.add(PageDescriptor{.id = FlashViewModel::pageId,
                             .titleSourceText = QT_TRANSLATE_NOOP("Pages", "Flash"),
                             .icon = QStringLiteral("flash"),
                             .qml = qmlUrl("pages/FlashPage.qml"),
                             .order = 20,
                             .global = false,
                             .createViewModel = [services](DeviceTabViewModel* tab) -> QObject* {
                                 // Qt parent ownership: the tab.
                                 // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
                                 return new FlashViewModel(tab, *services);
                             }});
    pages.add(PageDescriptor{.id = SettingsViewModel::pageId,
                             .titleSourceText = QT_TRANSLATE_NOOP("Pages", "Settings"),
                             .icon = QStringLiteral("settings"),
                             .qml = qmlUrl("pages/SettingsPage.qml"),
                             .order = 100,
                             .global = true,
                             .createViewModel = [&app, services](DeviceTabViewModel*) -> QObject* {
                                 // Qt parent ownership: AppViewModel (contract).
                                 // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
                                 return new SettingsViewModel(app, *services, &app);
                             }});

    // The sections of the Settings page (the Console section is 0005's, order 20).
    pages.addSettingsSection(
        SettingsSectionDescriptor{.id = QStringLiteral("general"),
                                  .titleSourceText = QT_TRANSLATE_NOOP("Pages", "General"),
                                  .qml = qmlUrl("settings/GeneralSection.qml"),
                                  .order = 10});
    pages.addSettingsSection(
        SettingsSectionDescriptor{.id = QStringLiteral("jlink"),
                                  .titleSourceText = QT_TRANSLATE_NOOP("Pages", "J-Link"),
                                  .qml = qmlUrl("settings/JLinkSection.qml"),
                                  .order = 30});
    pages.addSettingsSection(
        SettingsSectionDescriptor{.id = QStringLiteral("nrfutil"),
                                  .titleSourceText = QT_TRANSLATE_NOOP("Pages", "nRF Util"),
                                  .qml = qmlUrl("settings/NrfUtilSection.qml"),
                                  .order = 40});
    pages.addSettingsSection(
        SettingsSectionDescriptor{.id = QStringLiteral("about"),
                                  .titleSourceText = QT_TRANSLATE_NOOP("Pages", "About"),
                                  .qml = qmlUrl("settings/AboutSection.qml"),
                                  .order = 90});

    // ADR-0051: page shortcuts (descriptions in the "Shortcuts" context, review S5).
    ShortcutRegistry* shortcuts = app.shortcuts();
    logFailure("flash.start",
               shortcuts->add(QStringLiteral("flash.start"),
                              QKeySequence(QStringLiteral("Ctrl+Return")), FlashViewModel::pageId,
                              QT_TRANSLATE_NOOP("Shortcuts", "Flash")));
    logFailure("flash.browse",
               shortcuts->add(QStringLiteral("flash.browse"),
                              QKeySequence(QStringLiteral("Ctrl+O")), FlashViewModel::pageId,
                              QT_TRANSLATE_NOOP("Shortcuts", "Choose a firmware file")));
}

} // namespace hio::gui
