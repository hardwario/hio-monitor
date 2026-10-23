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
#include "hio/device/command_tree.h"
#include "hio/device/console_line.h"
#include "hio/device/session_registry.h"
#include "hio/firmware/firmware_image.h"
#include "http_fixture.h"
#include "language_manager.h"
#include "pages/builtin_pages.h"
#include "support/fake_jlink_scenario.h"
#include "support/test_page_view_model.h"
#include "support/test_wizard.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/console_view_model.h"
#include "viewmodels/device_tab_list_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "viewmodels/history_store.h"
#include "zip_builder.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QIODevice>
#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariant>
#include <QtContainerFwd>
#include <QtCore/qtmetamacros.h>
#include <QtEnvironmentVariables>
#include <QtLogging>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest/quicktest.h>
#include <QtTypes>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

Q_IMPORT_QML_PLUGIN(HardwarioMonitorPlugin)

// Test hooks for the QML tests (context property `testSupport`): things QML must not do in the
// product (configure a tab without the picker, fabricate errors, reset the tabs).
class QmlTestSupport : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString screenshotDir READ screenshotDir CONSTANT)
    // The console performance test (plan 0005) runs only with HIO_CONSOLE_PERF_SECONDS > 0.
    Q_PROPERTY(int perfSeconds READ perfSeconds CONSTANT)

public:
    QmlTestSupport(hio::gui::AppViewModel* app, QString directory)
        : m_app(app), m_dir(std::move(directory)) {}
    [[nodiscard]] static int perfSeconds() {
        return qEnvironmentVariableIntValue("HIO_CONSOLE_PERF_SECONDS");
    }

    // Another fake J-Link scenario for the sessions opened from now on ("" = the default one).
    Q_INVOKABLE [[nodiscard]] bool useScenario(const QString& json) const {
        const QByteArray utf8 = json.toUtf8();
        return hio::gui::test::useFakeJLink(
            m_dir, json.isEmpty() ? hio::gui::test::twoProbesScenario : utf8.constData());
    }

    // VmRSS of this process in MiB (Linux; 0 elsewhere).
    Q_INVOKABLE [[nodiscard]] static double rssMegabytes() {
        QFile status(QStringLiteral("/proc/self/status"));
        if (!status.open(QIODevice::ReadOnly)) {
            return 0;
        }
        for (const QByteArray& line : status.readAll().split('\n')) {
            if (line.startsWith("VmRSS:")) {
                return line.mid(6).trimmed().split(' ').value(0).toDouble() / 1024.0;
            }
        }
        return 0;
    }

    // Blocks the GUI thread (forces the session's event queue to overflow).
    Q_INVOKABLE static void blockGuiThread(int milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }

    // The newest sequence number of a tab's console log (lines received so far).
    Q_INVOKABLE [[nodiscard]] double consoleLastSeq(int index) const {
        hio::gui::DeviceTabViewModel* tab = m_app->tabs()->at(index);
        return tab != nullptr ? static_cast<double>(tab->consoleLog()->lastSeq()) : 0;
    }

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

    // Shows a connection error in the tab's ErrorPanel (as a failed open() would).
    Q_INVOKABLE void setConnectionError(int index, const QString& code) {
        if (hio::gui::DeviceTabViewModel* tab = m_app->tabs()->at(index); tab != nullptr) {
            tab->connection()->setOpenError(
                hio::makeError(hio::errorCodeFromId(code.toStdString())
                                   .value_or(hio::ErrorCode::InternalUnexpected)));
        }
    }

    // A session warning in the tab's shell banner (as DeviceSession::warning would give it).
    Q_INVOKABLE void setConnectionWarning(int index, const QString& code) {
        if (hio::gui::DeviceTabViewModel* tab = m_app->tabs()->at(index); tab != nullptr) {
            hio::Error warning = hio::makeError(
                hio::errorCodeFromId(code.toStdString())
                    .value_or(hio::ErrorCode::InternalUnexpected),
                code == QStringLiteral("io.write_failed")
                    ? "console/chester-app-20261016-101500-801000001.log: No space left on device"
                    : "J-Link 801000001 firmware: compiled Mar 3 2021");
            tab->connection()->setWarning(warning);
        }
    }

    // ---- console (plan 0005): lines without a device ----
    Q_INVOKABLE [[nodiscard]] QObject* consoleViewModel(int index) const {
        hio::gui::DeviceTabViewModel* tab = m_app->tabs()->at(index);
        return tab != nullptr ? tab->pageViewModel(QStringLiteral("console")) : nullptr;
    }

    // channel 0 = shell, 1 = log; level 0 none, 1 D, 2 I, 3 W, 4 E; direction 0 out, 1 in, 2
    // marker.
    // "%1" in `text` is replaced by the line number.
    Q_INVOKABLE void appendConsoleLines(int index, int channel, int count, const QString& text,
                                        int level, int direction) {
        hio::gui::DeviceTabViewModel* tab = m_app->tabs()->at(index);
        if (tab == nullptr) {
            return;
        }
        for (int i = 0; i < count; ++i) {
            hio::device::ConsoleLine line;
            line.channel = channel == 0 ? hio::device::Channel::Shell : hio::device::Channel::Log;
            line.dir = static_cast<hio::device::Direction>(direction);
            line.level = static_cast<hio::device::LogLevel>(level);
            line.time = std::chrono::system_clock::now();
            line.text = (text.contains(QStringLiteral("%1")) ? text.arg(i) : text).toStdString();
            tab->consoleLog()->append(std::move(line));
        }
        if (auto* console = qobject_cast<hio::gui::ConsoleViewModel*>(consoleViewModel(index));
            console != nullptr) {
            console->refreshNow();
        }
    }

    // Plan 0021: a command in the tab's shared history (as if it had been sent).
    Q_INVOKABLE void addConsoleHistory(int index, const QString& command) const {
        if (const hio::gui::DeviceTabViewModel* tab = m_app->tabs()->at(index); tab != nullptr) {
            m_app->historyStore().history(tab->profileId(), tab->consoleTargetId())->add(command);
        }
    }

    // Plan 0021: a learned top-level command tree for the tab's console (phase 2 learns it).
    Q_INVOKABLE void setConsoleCommands(int index, const QStringList& names) const {
        auto* console = qobject_cast<hio::gui::ConsoleViewModel*>(consoleViewModel(index));
        if (console == nullptr) {
            return;
        }
        std::vector<hio::device::CommandEntry> entries;
        for (const QString& name : names) {
            entries.push_back(hio::device::CommandEntry{
                .name = name.toStdString(),
                .help = (name + QStringLiteral(" commands")).toStdString()});
        }
        hio::device::CommandTree tree;
        (void)tree.setChildren({}, entries);
        console->setCommandTree(std::move(tree));
    }

    Q_INVOKABLE void addDroppedLines(int index, int count) const {
        if (auto* console = qobject_cast<hio::gui::ConsoleViewModel*>(consoleViewModel(index));
            console != nullptr) {
            console->addDroppedLines(static_cast<quint64>(count));
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
        // Plan 0021: the designated LTE firmware comes from a local fixture (never the internet;
        // test-only overrides HIO_DESIGNATED_URL / HIO_DESIGNATED_SHA256).
        const QByteArray modem = hio::firmware::test::zipBytes(
            {{.name = "firmware.update.image.segments.0.hex", .data = ":00000001FF\n"}});
        const QByteArray bundle = hio::firmware::test::zipBytes(
            {{.name = "modem.zip", .data = modem.toStdString()},
             {.name = "application.hex",
              .data = hio::firmware::test::hexRecord(0, 0x1000, {1, 2, 3, 4}) +
                      hio::firmware::test::eofRecord()}});
        m_lteFirmware.emplace();
        m_lteFirmware->set(QStringLiteral("/chester/lte.zip"),
                           hio::firmware::test::HttpFixture::Response::ok(bundle));
        qputenv("HIO_DESIGNATED_URL",
                (m_lteFirmware->baseUrl() + QStringLiteral("/lte.zip")).toUtf8());
        qputenv("HIO_DESIGNATED_SHA256",
                QCryptographicHash::hash(bundle, QCryptographicHash::Sha256).toHex());
        // The catalog (test-only HIO_CATALOG_URL) answers one firmware page (ADR-0166); other IDs
        // are unknown (404).
        m_lteFirmware->set(
            QStringLiteral("/chester/001543e218a04ffdbf65082532202736"),
            hio::firmware::test::HttpFixture::Response::ok(QByteArrayLiteral(
                "<h2>hio-chester-push-fm:v4.0.1</h2><div>Download the HEX file:</div>"
                "<div>SHA256 checksum: "
                "0c7a2286d945c1e12bf9b4f0620beff519a8ade36f98c1da33527d18fa568f5a</div>"
                "<div>Upload date/time</div><div>2026-09-07 12:13:53</div>")));
        qputenv("HIO_CATALOG_URL", m_lteFirmware->baseUrl().toUtf8());
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
        // The test file has finished (TestCase calls Qt.quit()): stop the probe scanner before
        // QtQuickTest destroys the view and its TestResult objects, which the scanner's log
        // messages read through the test message handler (TSan, review 0005 round 1). The
        // application itself is destroyed with the next engine or in cleanupTestCase().
        if (m_app) {
            QObject::connect(engine, &QQmlEngine::quit, &*m_app, [this] {
                if (m_app) {
                    m_app->probeScanner().stop();
                }
            });
        }
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
    std::optional<hio::firmware::test::HttpFixture> m_lteFirmware; // plan 0021
    std::optional<hio::gui::AppViewModel> m_app;
    std::optional<hio::gui::LanguageManager> m_languageManager;
    std::optional<QmlTestSupport> m_support;
};

QUICK_TEST_MAIN_WITH_SETUP(tst_gui, Setup)

#include "tst_gui.moc"
