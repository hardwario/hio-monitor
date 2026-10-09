// `hio-monitor` - HARDWARIO Monitor desktop application (ADR-0003, ADR-0018).
//
// Startup: QGuiApplication -> application identity -> logging -> settings (an error is logged,
// not fatal) -> LanguageManager applies ui/language -> QML module HardwarioMonitor, type Main.
// Exceptions never leave main(): the body is wrapped at this top boundary (ADR-0007).

#include "gui_log.h"
#include "hio/core/app_identity.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/settings.h"
#include "language_manager.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QString>
#include <QStringLiteral>
#include <QtCore/qnamespace.h>
#include <QtQml/qqmlextensionplugin.h>

#include <exception>
#include <iostream>
#include <optional>
#include <utility>

Q_IMPORT_QML_PLUGIN(HardwarioMonitorPlugin)

namespace {

int run(int& argc, char** argv) {
    QGuiApplication app(argc, argv);
    hio::initApplicationIdentity();
    QGuiApplication::setApplicationDisplayName(QStringLiteral("HARDWARIO Monitor"));

    auto logging = hio::log::Logging::init(hio::log::AppKind::Gui, {});
    if (!logging) {
        std::cerr << "hio-monitor: " << logging.error().message << " (" << logging.error().detail
                  << ")\n";
        return 1;
    }

    std::optional<hio::Settings> settings;
    if (auto opened = hio::Settings::openDefault(); opened) {
        settings.emplace(std::move(*opened));
    } else {
        qCWarning(hio::gui::lcGui)
            << "Settings could not be opened, defaults are used:"
            << hio::toQString(opened.error().message) << hio::toQString(opened.error().detail);
    }

    // Ownership: a stack object; the application is its QObject parent only so that AppViewModel
    // can find it (QObject::findChild). It is destroyed before `app`, which removes it from the
    // parent's children, so the parent never deletes it.
    hio::gui::LanguageManager languageManager(std::move(settings), &app);

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
    return QGuiApplication::exec();
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
