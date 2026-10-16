// Qt Quick Test runner for app/gui (tst_*.qml in this directory).
//
// The setup mirrors main(): Material style, bundled fonts, an AppViewModel as a direct child of the
// application (settings, probe locks, library copies and console files in a temporary directory,
// so the user's files are never touched), the built-in pages plus the contract test page
// (qml/TestPage.qml), the fake J-Link library with two probes, and a LanguageManager.

#include "fonts.h"
#include "hex_builder.h"
#include "hio/core/app_identity.h"
#include "hio/core/error.h"
#include "hio/core/settings.h"
#include "hio/device/session_registry.h"
#include "hio/firmware/firmware_image.h"
#include "language_manager.h"
#include "pages/builtin_pages.h"
#include "support/fake_jlink_scenario.h"
#include "support/test_page_view_model.h"
#include "support/test_wizard.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/device_tab_list_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "zip_builder.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QIODevice>
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
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

Q_IMPORT_QML_PLUGIN(HardwarioMonitorPlugin)

// Test hooks for the QML tests (context property `testSupport`): things QML must not do in the
// product (configure a tab without the picker, fabricate errors, reset the tabs).
class QmlTestSupport : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString screenshotDir READ screenshotDir CONSTANT)

public:
    QmlTestSupport(hio::gui::AppViewModel* app, QString directory)
        : m_app(app), m_dir(std::move(directory)) {}

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

    // An error with its default texts and wizard (e.g. probe.open_failed -> troubleshooting).
    Q_INVOKABLE [[nodiscard]] static QVariant defaultError(const QString& code) {
        return QVariant::fromValue(
            hio::gui::ErrorInfo(hio::makeError(hio::errorCodeFromId(code.toStdString())
                                                   .value_or(hio::ErrorCode::InternalUnexpected))));
    }

    // The open test wizard (its test hooks: goTo, startOperation, finishOperation), or null.
    Q_INVOKABLE [[nodiscard]] QObject* openWizard() const { return m_app->wizard()->wizard(); }

    // ---- plan 0006 (Flash page) ----
    // The Flash page view model of a tab.
    Q_INVOKABLE [[nodiscard]] QObject* flashPage(int index) const {
        hio::gui::DeviceTabViewModel* tab = m_app->tabs()->at(index);
        return tab != nullptr ? tab->pageViewModel(QStringLiteral("flash")) : nullptr;
    }

    // Writes test firmware files into the test directory and returns their URLs: an Intel HEX
    // (`uicr`: with a UICR record of the application MCU), an LTE bundle, a modem firmware zip.
    Q_INVOKABLE [[nodiscard]] QUrl writeHex(const QString& name, int size, bool uicr) const {
        std::vector<hio::firmware::Segment> segments;
        segments.push_back(hio::firmware::Segment{
            .address = 0x1000,
            .data = std::vector<std::byte>(static_cast<std::size_t>(size), std::byte{0x5A})});
        if (uicr) {
            segments.push_back(hio::firmware::Segment{
                .address = 0x10001200, .data = std::vector<std::byte>(4, std::byte{1})});
        }
        const auto image = hio::firmware::FirmwareImage::fromSegments(std::move(segments));
        return write(name, image ? QByteArray::fromStdString(hio::firmware::test::toHex(*image))
                                 : QByteArray());
    }

    Q_INVOKABLE [[nodiscard]] QUrl writeBundle() const {
        const QByteArray modem = hio::firmware::test::zipBytes(
            {{.name = "firmware.update.image.segments.0.hex", .data = ":00000001FF\n"}});
        const QUrl hex = writeHex(QStringLiteral("application.hex"), 4096, false);
        QFile file(hex.toLocalFile());
        (void)file.open(QIODevice::ReadOnly);
        return write(QStringLiteral("chester-lte-v3.0.0.zip"),
                     hio::firmware::test::zipBytes(
                         {{.name = "modem.zip", .data = modem.toStdString()},
                          {.name = "application.hex", .data = file.readAll().toStdString()}}));
    }

    Q_INVOKABLE [[nodiscard]] QUrl writeModemZip() const {
        return write(QStringLiteral("mfw_nrf91x1_2.0.2.zip"),
                     hio::firmware::test::zipBytes({{.name = "firmware.update.image.segments.0.hex",
                                                     .data = ":00000001FF\n"}}));
    }

    // Replaces the fake J-Link scenario (used by the next library copy, i.e. the next session).
    Q_INVOKABLE [[nodiscard]] bool useScenario(const QString& json) const {
        return hio::gui::test::useFakeJLink(m_dir, json.toUtf8().constData());
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
    [[nodiscard]] QUrl write(const QString& name, const QByteArray& bytes) const {
        const QString path = m_dir + QLatin1Char('/') + name;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return {};
        }
        file.write(bytes);
        return QUrl::fromLocalFile(path);
    }

    hio::gui::AppViewModel* m_app;
    QString m_dir;
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
        // No nRF Util in the Qt Quick tests (whatever is installed): deterministic prerequisites.
        qputenv("HIO_NRFUTIL", QFile::encodeName(m_dir.path() + QStringLiteral("/no-nrfutil")));
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
                        // M4 (plan/features/0007): the test wizard as "connect-device".
                        hio::gui::test::registerTestWizard(
                            app, QUrl::fromLocalFile(QStringLiteral(QUICK_TEST_SOURCE_DIR
                                                                    "/qml/TestEmbedded.qml")));
                    }},
            QCoreApplication::instance());
        m_languageManager.emplace(m_app->settingsStore());
        m_app->setLanguageManager(&*m_languageManager);
        // The tests start in English whatever the system language is.
        if (!m_languageManager->apply(QStringLiteral("en"))) {
            qFatal("tst_gui: English could not be selected");
        }
        m_support.emplace(&*m_app, m_dir.path());
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
