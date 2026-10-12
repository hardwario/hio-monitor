// Qt Quick Test runner for app/gui (tst_*.qml in this directory).
//
// The setup mirrors main(): Material style, bundled fonts, an AppViewModel as a direct child of the
// application (settings, probe locks, library copies and console files in a temporary directory,
// so the user's files are never touched), the built-in pages plus the contract test page
// (qml/TestPage.qml), the fake J-Link library with two probes, and a LanguageManager.

#include "fonts.h"
#include "hio/core/app_identity.h"
#include "hio/core/error.h"
#include "hio/core/settings.h"
#include "hio/device/session_registry.h"
#include "language_manager.h"
#include "pages/builtin_pages.h"
#include "support/fake_jlink_scenario.h"
#include "support/test_page_view_model.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/device_tab_list_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"

#include <QCoreApplication>
#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariant>
#include <QtCore/qtmetamacros.h>
#include <QtEnvironmentVariables>
#include <QtLogging>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest/quicktest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>

Q_IMPORT_QML_PLUGIN(HardwarioMonitorPlugin)

// Test hooks for the QML tests (context property `testSupport`): things QML must not do in the
// product (configure a tab without the picker, fabricate errors, reset the tabs).
class QmlTestSupport : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString screenshotDir READ screenshotDir CONSTANT)

public:
    explicit QmlTestSupport(hio::gui::AppViewModel* app) : m_app(app) {}

    [[nodiscard]] static QString screenshotDir() {
        return qEnvironmentVariable("HIO_SCREENSHOT_DIR");
    }

    Q_INVOKABLE bool configureTab(int index, const QString& target, int serial,
                                  const QString& product) {
        hio::gui::DeviceTabViewModel* tab = m_app->tabs()->at(index);
        return tab != nullptr && tab->configure(QStringLiteral("chester"), target,
                                                static_cast<std::uint32_t>(serial), product);
    }

    // Keeps one Choose device tab (busy pages are finished first).
    Q_INVOKABLE void resetTabs() {
        while (m_app->tabs()->count() > 1) {
            finishWork(m_app->tabs()->count() - 1);
            m_app->closeTab(m_app->tabs()->count() - 1);
        }
        finishWork(0);
        m_app->closeTab(0); // replaced by a new Choose device tab
        m_app->closeGlobalPage();
        m_app->setShortcutOverlayVisible(false);
    }

    Q_INVOKABLE [[nodiscard]] QObject* testPage(int index) const {
        hio::gui::DeviceTabViewModel* tab = m_app->tabs()->at(index);
        return tab != nullptr ? tab->pageViewModel(QStringLiteral("test")) : nullptr;
    }

    Q_INVOKABLE void finishWork(int index) {
        if (hio::gui::DeviceTabViewModel* tab = m_app->tabs()->at(index); tab != nullptr) {
            tab->clearBusy(QStringLiteral("test"));
        }
    }

    Q_INVOKABLE [[nodiscard]] static QVariant error(const QString& code, bool withWizard) {
        hio::Error error = hio::makeError(
            hio::errorCodeFromId(code.toStdString()).value_or(hio::ErrorCode::InternalUnexpected),
            "VTref = 0 mV · J-Link 801000001");
        error.message = "The device is not powered.";
        error.cause = "the target voltage is 0.0 V.";
        error.suggestion = "connect power, then reconnect.";
        if (withWizard) {
            error.wizard = hio::WizardId::ConnectDevice;
        } else {
            error.wizard.reset();
        }
        return QVariant::fromValue(hio::gui::ErrorInfo(error));
    }

    // Shows a connection error in the tab's ErrorPanel (as a failed open() would).
    Q_INVOKABLE void setConnectionError(int index, const QString& code) {
        if (hio::gui::DeviceTabViewModel* tab = m_app->tabs()->at(index); tab != nullptr) {
            tab->connection()->setOpenError(
                hio::makeError(hio::errorCodeFromId(code.toStdString())
                                   .value_or(hio::ErrorCode::InternalUnexpected)));
        }
    }

private:
    hio::gui::AppViewModel* m_app;
};

class Setup : public QObject {
    Q_OBJECT

public slots:
    void applicationAvailable() {
        QStandardPaths::setTestModeEnabled(true);
        hio::initApplicationIdentity();
        QQuickStyle::setStyle(QStringLiteral("Material"));
        hio::gui::loadBundledFonts();
        (void)hio::gui::test::useFakeJLink(m_dir.path(), hio::gui::test::twoProbesScenario);
    }

    // One AppViewModel per test file (per QML engine): QML bindings of one engine never outlive it
    // on a shared C++ object (seen as leaked binding guards under LeakSanitizer otherwise).
    void createApp() {
        const std::filesystem::path root(m_dir.path().toStdU16String());
        std::optional<hio::Settings> settings;
        if (auto opened = hio::Settings::openFile(root / "Monitor.ini"); opened) {
            settings.emplace(std::move(*opened));
        }
        // Ownership: held here; the application is only the QObject parent so that the QML
        // singleton factory finds it. Destroyed by the next createApp() or cleanupTestCase(), i.e.
        // before the application.
        m_app.emplace(
            hio::gui::AppOptions{
                .settings = std::move(settings),
                .registry = hio::device::RegistryOptions{.instanceRoot = root / "instances",
                                                         .lockDirectory = root / "locks",
                                                         .consoleDirectory = root / "console"},
                .scanInterval = std::chrono::milliseconds(300),
                .registerExtensions =
                    [](hio::gui::AppViewModel& app) {
                        hio::gui::registerBuiltInPages(app);
                        hio::gui::test::registerTestPage(
                            app, QUrl::fromLocalFile(
                                     QStringLiteral(QUICK_TEST_SOURCE_DIR "/qml/TestPage.qml")));
                    }},
            QCoreApplication::instance());
        m_languageManager.emplace(m_app->settingsStore());
        m_app->setLanguageManager(&*m_languageManager);
        // The tests start in English whatever the system language is.
        if (!m_languageManager->apply(QStringLiteral("en"))) {
            qFatal("tst_gui: English could not be selected");
        }
        m_support.emplace(&*m_app);
    }

    void destroyApp() {
        m_support.reset();
        m_languageManager.reset();
        m_app.reset();
    }

    void qmlEngineAvailable(QQmlEngine* engine) {
        destroyApp();
        createApp();
        if (m_languageManager) {
            m_languageManager->setEngine(engine);
        }
        if (m_support) {
            engine->rootContext()->setContextProperty(QStringLiteral("testSupport"), &*m_support);
        }
    }

    void cleanupTestCase() { destroyApp(); }

private:
    QTemporaryDir m_dir;
    std::optional<hio::gui::AppViewModel> m_app;
    std::optional<hio::gui::LanguageManager> m_languageManager;
    std::optional<QmlTestSupport> m_support;
};

QUICK_TEST_MAIN_WITH_SETUP(tst_gui, Setup)

#include "tst_gui.moc"
