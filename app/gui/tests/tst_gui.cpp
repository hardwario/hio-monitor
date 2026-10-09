// Qt Quick Test runner for app/gui (tst_*.qml in this directory).
//
// The setup mirrors main(): application identity and a LanguageManager as a direct child of the
// application, with settings in a temporary file so the user's settings are never touched.

#include "hio/core/app_identity.h"
#include "hio/core/settings.h"
#include "language_manager.h"

#include <QCoreApplication>
#include <QObject>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtCore/qtmetamacros.h>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest/quicktest.h>

#include <filesystem>
#include <optional>
#include <utility>

Q_IMPORT_QML_PLUGIN(HardwarioMonitorPlugin)

class Setup : public QObject {
    Q_OBJECT

public slots:
    void applicationAvailable() {
        QStandardPaths::setTestModeEnabled(true);
        hio::initApplicationIdentity();
        std::optional<hio::Settings> settings;
        if (auto opened = hio::Settings::openFile(
                std::filesystem::path(m_dir.path().toStdU16String()) / "Monitor.ini");
            opened) {
            settings.emplace(std::move(*opened));
        }
        // Ownership: held here; the application is only the QObject parent so that AppViewModel
        // finds it. Destroyed in cleanupTestCase(), i.e. before the application.
        m_languageManager.emplace(std::move(settings), QCoreApplication::instance());
    }

    void qmlEngineAvailable(QQmlEngine* engine) {
        if (m_languageManager) {
            m_languageManager->setEngine(engine);
        }
    }

    void cleanupTestCase() { m_languageManager.reset(); }

private:
    QTemporaryDir m_dir;
    std::optional<hio::gui::LanguageManager> m_languageManager;
};

QUICK_TEST_MAIN_WITH_SETUP(tst_gui, Setup)

#include "tst_gui.moc"
