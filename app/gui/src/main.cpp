// `hwio-monitor` - HARDWARIO Monitor desktop application (ADR-0003, ADR-0018;
// plan/features/0004-m3-gui-shell.md).
//
// Startup: QGuiApplication -> application identity -> logging -> Material style, bundled fonts ->
// single instance (ADR-0058: a second launch activates the first window and exits; a hung first
// instance -> "Quit / Start anyway") -> settings (an error is logged, not fatal) -> AppViewModel
// (settings store, registries, probe scanner, restored tabs) -> LanguageManager applies
// ui/language -> QML module HardwarioMonitor, type Main.
// Exceptions never leave main(): the body is wrapped at this top boundary (ADR-0007).
//
// Hidden option --self-test (plan/features/0015 A2): loads the main QML with default settings
// (nothing is read or written), without the single-instance lock, and exits 0 when the window was
// created (1 otherwise). The installed-tree CTest runs it with QT_QPA_PLATFORM=offscreen to prove
// that the deployed tree (Qt libraries, plugins, QML modules) is complete.

#include "fonts.h"
#include "gui_log.h"
#include "hwio/core/app_identity.h"
#include "hwio/core/crash_handler.h"
#include "hwio/core/json.h"
#include "hwio/core/log.h"
#include "hwio/core/settings.h"
#include "hwio/core/version.h"
#include "hwio/device/session_registry.h"
#include "language_manager.h"
#include "pages/builtin_pages.h"
#include "platform.h"
#include "settings_store.h"
#include "single_instance.h"
#include "viewmodels/app_view_model.h"
#include "workers/probe_scanner.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QNetworkProxyFactory>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QString>
#include <QStringLiteral>
#include <QStyleHints>
#include <QTimer>
#include <QVariant>
#include <QWindow>
#include <QtCore/qnamespace.h>
#include <QtEnvironmentVariables>
#include <QtQml/qqmlextensionplugin.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

Q_IMPORT_QML_PLUGIN(HardwarioMonitorPlugin)

namespace {

// The application icon in all embedded sizes (plan 0015 A3).
QIcon applicationIcon() {
    QIcon icon;
    for (const int size : std::array{16, 32, 48, 64, 128, 256}) {
        icon.addFile(QStringLiteral(":/appicon/hwio-monitor-%1.png").arg(size));
    }
    return icon;
}

// "HARDWARIO Monitor is already running but does not respond." [Quit] [Start anyway].
// True for "Start anyway".
bool askStartAnyway() {
    // A minimal context (review S7): the settings for the language and the theme, no
    // AppViewModel (no scanner thread, no registry); the dialog does not use the AppViewModel
    // singleton.
    std::optional<hwio::Settings> settings;
    if (auto opened = hwio::Settings::openDefault(); opened) {
        settings.emplace(std::move(*opened));
    }
    hwio::gui::SettingsStore store(std::move(settings));
    hwio::gui::AppViewModel::registerShellKeys(store);
    const hwio::gui::LanguageManager language(&store);
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
    hwio::gui::applyPlatformIdentity();       // Windows AppUserModelID, before the first window
    (void)hwio::gui::applyTextAntialiasing(); // ADR-0205: before the QGuiApplication
    QGuiApplication app(argc, argv);
    hwio::initApplicationIdentity();
    QGuiApplication::setApplicationDisplayName(QStringLiteral("HARDWARIO Monitor"));
    // ADR-0018: the desktop file / Wayland app id (packaging/linux/com.hardwario.monitor.desktop).
    QGuiApplication::setDesktopFileName(QStringLiteral("com.hardwario.monitor"));
    QGuiApplication::setWindowIcon(applicationIcon());
    const bool selfTest = QCoreApplication::arguments().contains(QStringLiteral("--self-test"));
    // Plan 0016: firmware downloads and the update check use the system proxy settings.
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    auto logging = hwio::log::Logging::init(hwio::log::AppKind::Gui, {});
    if (!logging) {
        std::cerr << "hwio-monitor: " << logging.error().message << " (" << logging.error().detail
                  << ")\n";
        return 1;
    }

    // ADR-0044: Material (Dense via :/qtquickcontrols2.conf), set before any QML is loaded.
    QQuickStyle::setStyle(QStringLiteral("Material"));
    hwio::gui::loadBundledFonts();

    hwio::gui::SingleInstance instance; // not started by the self-test
    if (!selfTest) {
        switch (instance.start()) {
        case hwio::gui::SingleInstance::Result::Primary:
            break;
        case hwio::gui::SingleInstance::Result::Activated:
            return 0;
        case hwio::gui::SingleInstance::Result::NotResponding:
            if (!askStartAnyway()) {
                return 0;
            }
            instance.startWithoutLock();
            break;
        }
    }

    std::optional<hwio::Settings> settings;
    if (selfTest) {
        // Default settings in memory: the self-test reads and writes no settings file.
    } else if (auto opened = hwio::Settings::openDefault(); opened) {
        settings.emplace(std::move(*opened));
    } else {
        qCWarning(hwio::gui::lcGui)
            << "Settings could not be opened, defaults are used:"
            << hwio::toQString(opened.error().message) << hwio::toQString(opened.error().detail);
    }

    // Ownership: stack objects. The application is AppViewModel's QObject parent only so that the
    // QML singleton factory finds it (QObject::findChild); it is destroyed before `app`, which
    // removes it from the parent's children. The language manager is destroyed first.
    hwio::gui::AppViewModel appViewModel(
        hwio::gui::AppOptions{.settings = std::move(settings),
                              .registry = hwio::device::RegistryOptions::defaults(),
                              .scanInterval = hwio::gui::ProbeScanner::defaultInterval,
                              .registerExtensions = &hwio::gui::registerBuiltInPages,
                              // The self-test shows no crash notice (and acknowledges nothing).
                              .crashDirectory = selfTest ? std::filesystem::path()
                                                         : hwio::crash::defaultDirectory()},
        &app);
    hwio::gui::LanguageManager languageManager(appViewModel.settingsStore());
    appViewModel.setLanguageManager(&languageManager);
    QObject::connect(&instance, &hwio::gui::SingleInstance::activationRequested, &appViewModel,
                     &hwio::gui::AppViewModel::activateWindow);

    QQmlApplicationEngine engine;
    languageManager.setEngine(&engine);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] {
            qCCritical(hwio::gui::lcGui) << "The main window could not be created";
            QCoreApplication::exit(1);
        },
        Qt::QueuedConnection);
    engine.loadFromModule("HardwarioMonitor", "Main");
    if (engine.rootObjects().isEmpty()) {
        qCCritical(hwio::gui::lcGui) << "The main window could not be created";
        return 1;
    }
    appViewModel.attachWindow(qobject_cast<QWindow*>(engine.rootObjects().constFirst()));
    if (selfTest) {
        std::cout << "hwio-monitor self-test: the main window was created\n";
        return 0;
    }
#ifndef NDEBUG
    // Plan 0015 manual check: HWIO_TEST_CRASH=1 (debug builds only) aborts 3 s after the start, so
    // that the next start shows the crash notice.
    if (qEnvironmentVariableIsSet("HWIO_TEST_CRASH")) {
        QTimer::singleShot(std::chrono::seconds(3), &app, [] { std::abort(); });
    }
#endif
    const int code = QGuiApplication::exec();
    appViewModel.saveState();
    return code;
}

} // namespace

int main(int argc, char** argv) {
    try {
        // ADR-0123: local crash files, first thing (the identity gives the data directory).
        hwio::initApplicationIdentity();
        const hwio::BuildInfo build = hwio::buildInfo();
        const std::string version = std::format("{} (git {})", build.version, build.gitHash);
        if (auto crash = hwio::crash::install({.directory = hwio::crash::defaultDirectory(),
                                               .appName = "hwio-monitor",
                                               .version = version});
            !crash) {
            std::cerr << "hwio-monitor: crash handler not installed: " << crash.error().detail
                      << '\n';
        }
        return run(argc, argv);
    } catch (const std::exception& exception) {
        std::cerr << "hwio-monitor: unexpected internal error: " << exception.what() << '\n';
    } catch (...) {
        std::cerr << "hwio-monitor: unexpected internal error\n";
    }
    return 1;
}
