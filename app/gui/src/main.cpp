// `hio-monitor` - HARDWARIO Monitor desktop application (ADR-0003, ADR-0018;
// plan/features/0004-m3-gui-shell.md).
//
// Startup: QGuiApplication -> application identity -> logging -> Material style, bundled fonts ->
// single instance (ADR-0058: a second launch activates the first window and exits; a hung first
// instance -> "Quit / Start anyway") -> settings (an error is logged, not fatal) -> AppViewModel
// (settings store, registries, probe scanner, restored tabs) -> LanguageManager applies
// ui/language -> QML module HardwarioMonitor, type Main.
// Exceptions never leave main(): the body is wrapped at this top boundary (ADR-0007).

#include "fonts.h"
#include "gui_log.h"
#include "hio/core/app_identity.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/settings.h"
#include "hio/device/session_registry.h"
#include "language_manager.h"
#include "pages/builtin_pages.h"
#include "settings_store.h"
#include "single_instance.h"
#include "viewmodels/app_view_model.h"
#include "workers/probe_scanner.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QNetworkProxyFactory>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QString>
#include <QStringLiteral>
#include <QStyleHints>
#include <QVariant>
#include <QWindow>
#include <QtCore/qnamespace.h>
#include <QtQml/qqmlextensionplugin.h>

#include <exception>
#include <iostream>
#include <optional>
#include <utility>

Q_IMPORT_QML_PLUGIN(HardwarioMonitorPlugin)

namespace {

// "HARDWARIO Monitor is already running but does not respond." [Quit] [Start anyway].
// True for "Start anyway".
bool askStartAnyway() {
    // A minimal context (review S7): the settings for the language and the theme, no
    // AppViewModel (no scanner thread, no registry); the dialog does not use the AppViewModel
    // singleton.
    std::optional<hio::Settings> settings;
    if (auto opened = hio::Settings::openDefault(); opened) {
        settings.emplace(std::move(*opened));
    }
    hio::gui::SettingsStore store(std::move(settings));
    hio::gui::AppViewModel::registerShellKeys(store);
    const hio::gui::LanguageManager language(&store);
    const QString theme = store.value(QStringLiteral("ui/theme")).toString();
    const bool dark = theme == QStringLiteral("dark") ||
                      (theme == QStringLiteral("system") &&
                       QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);

    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("darkTheme"), dark}});
    engine.loadFromModule("HardwarioMonitor", "AlreadyRunningDialog");
    if (engine.rootObjects().isEmpty()) {
        return false;
    }
    auto* window = qobject_cast<QWindow*>(engine.rootObjects().constFirst());
    if (window == nullptr) {
        return false;
    }
    QEventLoop loop;
    QObject::connect(window, &QWindow::visibleChanged, &loop, [&loop](bool visible) {
        if (!visible) {
            loop.quit();
        }
    });
    loop.exec();
    return window->property("startAnyway").toBool();
}

int run(int& argc, char** argv) {
    QGuiApplication app(argc, argv);
    hio::initApplicationIdentity();
    QGuiApplication::setApplicationDisplayName(QStringLiteral("HARDWARIO Monitor"));
    // Plan 0016: firmware downloads and the update check use the system proxy settings.
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    auto logging = hio::log::Logging::init(hio::log::AppKind::Gui, {});
    if (!logging) {
        std::cerr << "hio-monitor: " << logging.error().message << " (" << logging.error().detail
                  << ")\n";
        return 1;
    }

    // ADR-0044: Material (Dense via :/qtquickcontrols2.conf), set before any QML is loaded.
    QQuickStyle::setStyle(QStringLiteral("Material"));
    hio::gui::loadBundledFonts();

    hio::gui::SingleInstance instance;
    switch (instance.start()) {
    case hio::gui::SingleInstance::Result::Primary:
        break;
    case hio::gui::SingleInstance::Result::Activated:
        return 0;
    case hio::gui::SingleInstance::Result::NotResponding:
        if (!askStartAnyway()) {
            return 0;
        }
        instance.startWithoutLock();
        break;
    }

    std::optional<hio::Settings> settings;
    if (auto opened = hio::Settings::openDefault(); opened) {
        settings.emplace(std::move(*opened));
    } else {
        qCWarning(hio::gui::lcGui)
            << "Settings could not be opened, defaults are used:"
            << hio::toQString(opened.error().message) << hio::toQString(opened.error().detail);
    }

    // Ownership: stack objects. The application is AppViewModel's QObject parent only so that the
    // QML singleton factory finds it (QObject::findChild); it is destroyed before `app`, which
    // removes it from the parent's children. The language manager is destroyed first.
    hio::gui::AppViewModel appViewModel(
        hio::gui::AppOptions{.settings = std::move(settings),
                             .registry = hio::device::RegistryOptions::defaults(),
                             .scanInterval = hio::gui::ProbeScanner::defaultInterval,
                             .registerExtensions = &hio::gui::registerBuiltInPages},
        &app);
    hio::gui::LanguageManager languageManager(appViewModel.settingsStore());
    appViewModel.setLanguageManager(&languageManager);
    QObject::connect(&instance, &hio::gui::SingleInstance::activationRequested, &appViewModel,
                     &hio::gui::AppViewModel::activateWindow);

    QQmlApplicationEngine engine;
    languageManager.setEngine(&engine);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] {
            qCCritical(hio::gui::lcGui) << "The main window could not be created";
            QCoreApplication::exit(1);
        },
        Qt::QueuedConnection);
    engine.loadFromModule("HardwarioMonitor", "Main");
    if (engine.rootObjects().isEmpty()) {
        qCCritical(hio::gui::lcGui) << "The main window could not be created";
        return 1;
    }
    appViewModel.attachWindow(qobject_cast<QWindow*>(engine.rootObjects().constFirst()));
    const int code = QGuiApplication::exec();
    appViewModel.saveState();
    return code;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& exception) {
        std::cerr << "hio-monitor: unexpected internal error: " << exception.what() << '\n';
    } catch (...) {
        std::cerr << "hio-monitor: unexpected internal error\n";
    }
    return 1;
}
