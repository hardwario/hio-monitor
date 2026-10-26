// Console HIL with a CHESTER and a J-Link (plan/features/0005-m3-console.md, Tests: HIL). Skipped
// unless HIO_HIL=1 and HIO_HIL_SN names the probe (the GUI tab needs a serial number). Harmless
// shell commands only ("help", "app config show") and Reset (restarts the firmware); never
// flashing or configuration changes. Settings, history and console files live in a temporary
// directory. The two-tab streaming check needs two probes and stays manual.

#include "gui_test_support.h"
#include "hio/core/json.h"
#include "hio/core/settings.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/session_registry.h"
#include "pages/builtin_pages.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/console_line_model.h"
#include "viewmodels/console_view_model.h"
#include "viewmodels/device_tab_view_model.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTemporaryDir>
#include <QUrl>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace {

using hio::device::Channel;
using hio::device::Direction;
using hio::gui::AppViewModel;
using hio::gui::ConsoleLineModel;
using hio::gui::ConsoleViewModel;
using hio::gui::DeviceTabViewModel;
using hio::gui::test::waitFor;
using std::chrono::seconds;

class ConsoleHil : public ::testing::Test {
protected:
    void SetUp() override {
        if (qEnvironmentVariable("HIO_HIL") != QStringLiteral("1")) {
            GTEST_SKIP() << "HIO_HIL=1 is not set";
        }
        bool ok = false;
        m_serial = qEnvironmentVariable("HIO_HIL_SN").toUInt(&ok);
        if (!ok) {
            GTEST_SKIP() << "HIO_HIL_SN is not set";
        }
        createApp();
    }

    void createApp() {
        const std::filesystem::path root(m_dir.path().toStdU16String());
        std::optional<hio::Settings> settings;
        if (auto opened = hio::Settings::openFile(root / "Monitor.ini"); opened) {
            settings.emplace(std::move(*opened));
        }
        m_app = std::make_unique<AppViewModel>(hio::gui::AppOptions{
            .settings = std::move(settings),
            .registry = hio::device::RegistryOptions{.instanceRoot = root / "instances",
                                                     .lockDirectory = root / "locks",
                                                     .consoleDirectory = root / "console"},
            .scanInterval = std::chrono::milliseconds(2000),
            .registerExtensions = &hio::gui::registerBuiltInPages});
        m_tab = m_app->currentTabViewModel();
        ASSERT_TRUE(m_tab->configure(QStringLiteral("chester"), QStringLiteral("app"), m_serial));
    }

    [[nodiscard]] ConsoleViewModel& console() const {
        return *qobject_cast<ConsoleViewModel*>(m_tab->pageViewModel(QStringLiteral("console")));
    }

    [[nodiscard]] bool connect() const {
        m_tab->connectDevice();
        return waitFor([this] { return m_tab->state() == DeviceTabViewModel::Connected; },
                       seconds(60));
    }

    // Shell output lines (from the device) newer than `after`.
    [[nodiscard]] int shellOutputSince(std::uint64_t after) const {
        int count = 0;
        for (const auto& line :
             m_tab->consoleLog()->linesSince(after, hio::device::ChannelMask::Shell)) {
            count += line.dir == Direction::Out ? 1 : 0;
        }
        return count;
    }

    [[nodiscard]] int echoes(const std::string& command) const {
        int count = 0;
        for (const auto& line :
             m_tab->consoleLog()->linesSince(0, hio::device::ChannelMask::Shell)) {
            count += line.dir == Direction::In && line.text == command ? 1 : 0;
        }
        return count;
    }

    [[nodiscard]] DeviceTabViewModel* tab() const { return m_tab; }
    [[nodiscard]] QString dir() const { return m_dir.path(); }
    void destroyApp() { m_app.reset(); }

private:
    QTemporaryDir m_dir;
    std::uint32_t m_serial = 0;
    std::unique_ptr<AppViewModel> m_app;
    DeviceTabViewModel* m_tab = nullptr;
};

TEST_F(ConsoleHil, CommandsBatchHistoryAndReset) {
    const auto start = std::chrono::steady_clock::now();
    ASSERT_TRUE(connect()) << "not connected";
    std::cout << "[ hil ] connected after "
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - start)
                     .count()
              << " ms\n";
    ASSERT_TRUE(console().inputEnabled());

    // Harmless commands: the answer arrives in the shell view.
    for (const char* command : {"help", "app config show"}) {
        const std::uint64_t before = tab()->consoleLog()->lastSeq();
        console().send(QString::fromLatin1(command));
        ASSERT_TRUE(waitFor([&] { return shellOutputSince(before) >= 2; }, seconds(10))) << command;
        console().refreshNow();
        std::cout << "[ hil ] " << command << ": " << shellOutputSince(before) << " lines\n";
    }
    EXPECT_GT(console().shell()->rowCount(), 4);
    EXPECT_GT(console().log()->totalLines() + console().shell()->totalLines(), 4);

    // Batch of 40 commands, 200 ms apart.
    const std::filesystem::path batch = std::filesystem::path(dir().toStdU16String()) / "batch.txt";
    {
        std::ofstream out(batch);
        out << "# HIL batch\n";
        for (int i = 0; i < 40; ++i) {
            out << "help\n";
        }
    }
    const int helpBefore = echoes("help");
    const auto batchStart = std::chrono::steady_clock::now();
    console().runBatch(QUrl::fromLocalFile(hio::toQString(hio::toUtf8(batch))));
    ASSERT_TRUE(console().batchRunning());
    ASSERT_TRUE(waitFor([&] { return !console().batchRunning(); }, seconds(30)));
    EXPECT_FALSE(console().error().valid()) << console().error().message().toStdString();
    EXPECT_EQ(console().batchDone(), 40);
    ASSERT_TRUE(waitFor([&] { return echoes("help") == helpBefore + 40; }, seconds(10)))
        << echoes("help") - helpBefore;
    std::cout << "[ hil ] batch of 40 in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - batchStart)
                     .count()
              << " ms\n";

    // Reset (the session's reset operation): the console re-attaches and the device talks again.
    const std::uint64_t beforeReset = tab()->consoleLog()->lastSeq();
    console().reset(false);
    ASSERT_TRUE(waitFor([&] { return console().canReset(); }, seconds(30)));
    EXPECT_FALSE(console().error().valid()) << console().error().message().toStdString();
    // The firmware boots again: new output (shell or log) after the reset.
    const bool reattached = waitFor(
        [&] {
            return tab()->state() == DeviceTabViewModel::Connected &&
                   !tab()->consoleLog()->linesSince(beforeReset).empty();
        },
        seconds(60));
    EXPECT_TRUE(reattached) << "no output after the reset (see HIL finding H6)";
    if (reattached) {
        // The shell answers again (a stale control block right after the reset, H6, does not).
        bool answered = false;
        for (int attempt = 0; attempt < 3 && !answered; ++attempt) {
            const std::uint64_t before = tab()->consoleLog()->lastSeq();
            console().send(QStringLiteral("help"));
            answered = waitFor([&] { return shellOutputSince(before) >= 2; }, seconds(5));
            std::cout << "[ hil ] help after the reset, attempt " << attempt + 1 << ": "
                      << (answered ? "answered" : "no answer") << "\n";
        }
        EXPECT_TRUE(answered);
    }

    // The history survives the application (saved when it is destroyed).
    tab()->disconnectDevice();
    destroyApp();
    createApp();
    EXPECT_EQ(console().historyPrevious(QString()), QStringLiteral("help"));
    EXPECT_TRUE(console()
                    .historySearch(QStringLiteral("config"))
                    .contains(QStringLiteral("app config show")));
}

} // namespace
