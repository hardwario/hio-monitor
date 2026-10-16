// ConsoleViewModel against the fake J-Link library (plan 0005, C1/C4/C5): the tab's log across
// sessions with the shell's markers and no handlers left on an old session, sending and the
// shared history, input states, batch files (pacing, limits, queue-full, stop on disconnect),
// Reset through the session operation (ADR-0062), clear, save view, the session's console file,
// layout, and the review round 1 fixes (K8 history walk, K9 send result and busy batch, K12).

#include "gui_test_support.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/device_session.h"
#include "support/test_page_view_model.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/console_line_model.h"
#include "viewmodels/console_settings.h"
#include "viewmodels/console_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/history_store.h"
#include "workers/batch_runner.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QModelIndex>
#include <QObject>
#include <QPointer>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtContainerFwd>

#include <gtest/gtest.h>

#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using hio::ErrorCode;
using hio::device::Channel;
using hio::device::Direction;
using hio::gui::AppViewModel;
using hio::gui::BatchRunner;
using hio::gui::ConsoleLineModel;
using hio::gui::ConsoleViewModel;
using hio::gui::DeviceTabViewModel;
using hio::gui::test::spin;
using hio::gui::test::TestPageViewModel;
using hio::gui::test::waitFor;

constexpr const char* consoleScenario = R"({
  "libraryVersion": 81000,
  "probes": [
    {"serialNumber": 801000001, "product": "Test Probe",
     "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted A\n"},
                        {"atMs": 0, "buffer": 1, "text": "<inf> app: started\n"}],
             "responses": {"version": "Firmware version: v3.1.0\n"}}},
    {"serialNumber": 801000002, "product": "Test Probe 2",
     "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted B\n"}],
             "responses": {"version": "Firmware version: v9.9.9\n"}}}
  ]})";

class ConsoleViewModelTest : public hio::gui::test::GuiTest {
protected:
    void SetUp() override {
        GuiTest::SetUp();
        useScenario(consoleScenario);
        m_app = makeApp();
        m_tab = m_app->currentTabViewModel();
        ASSERT_TRUE(m_tab->configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001,
                                     QStringLiteral("Test Probe")));
    }
    void TearDown() override {
        m_app.reset();
        GuiTest::TearDown();
    }

    [[nodiscard]] AppViewModel& app() { return *m_app; }
    [[nodiscard]] DeviceTabViewModel& tab() { return *m_tab; }
    [[nodiscard]] ConsoleViewModel& console() { return *consoleOf(*m_tab); }
    [[nodiscard]] static ConsoleViewModel* consoleOf(DeviceTabViewModel& tab) {
        return qobject_cast<ConsoleViewModel*>(tab.pageViewModel(QStringLiteral("console")));
    }

    static bool connectAndWait(DeviceTabViewModel& tab) {
        tab.connectDevice();
        return waitFor([&] { return tab.state() == DeviceTabViewModel::Connected; });
    }

    [[nodiscard]] static QStringList rows(const ConsoleLineModel& model) {
        QStringList texts;
        for (int row = 0; row < model.rowCount(); ++row) {
            texts.append(model.data(model.index(row), ConsoleLineModel::TextRole).toString());
        }
        return texts;
    }

    [[nodiscard]] std::filesystem::path writeFile(const std::string& name,
                                                  const std::string& content) const {
        const std::filesystem::path path = root() / name;
        std::ofstream(path, std::ios::binary) << content;
        return path;
    }

    [[nodiscard]] static QUrl url(const std::filesystem::path& path) {
        return QUrl::fromLocalFile(hio::toQString(hio::toUtf8(path)));
    }

private:
    std::unique_ptr<AppViewModel> m_app;
    DeviceTabViewModel* m_tab = nullptr;
};

TEST_F(ConsoleViewModelTest, RegisteredAsTheConsolePage) {
    ASSERT_NE(consoleOf(tab()), nullptr);
    EXPECT_EQ(console().parent(), &tab());
    EXPECT_EQ(app().pageRegistry().find(QStringLiteral("console"))->qml,
              QUrl(QStringLiteral("qrc:/qt/qml/HardwarioMonitor/qml/pages/ConsolePage.qml")));
    ASSERT_EQ(app().pageRegistry().settingsSections().size(), 1U);
    EXPECT_EQ(app().pageRegistry().settingsSections().front().id, QStringLiteral("console"));
    EXPECT_FALSE(app().shortcuts()->portableText(QStringLiteral("console.find")).isEmpty());
    // F4 is the shell's (review S5): not in the console scope.
    EXPECT_TRUE(app().shortcuts()->portableText(QStringLiteral("console.reconnect")).isEmpty());
    EXPECT_TRUE(app().settingsStore()->isRegistered(QStringLiteral("console/fontSize")));
}

TEST_F(ConsoleViewModelTest, ShowsTheTabsLogAcrossSessions) {
    EXPECT_TRUE(console().showConnectState());
    EXPECT_FALSE(console().inputEnabled());
    EXPECT_EQ(console().inputDisabledReason(), QStringLiteral("Not connected"));

    ASSERT_TRUE(connectAndWait(tab()));
    EXPECT_TRUE(console().inputEnabled());
    EXPECT_FALSE(console().showConnectState());
    ASSERT_TRUE(waitFor(
        [&] { return console().shell()->rowCount() >= 2 && console().log()->rowCount() >= 2; }));
    EXPECT_TRUE(rows(*console().shell()).contains(QStringLiteral("uart:~$ booted A")));
    EXPECT_TRUE(rows(*console().log()).contains(QStringLiteral("<inf> app: started")));
    EXPECT_TRUE(rows(*console().shell()).contains(QStringLiteral("— connected —"))); // the shell's

    // Disconnect: no handlers of the console view model are left on the old session.
    QPointer<hio::device::DeviceSession> old(tab().session());
    bool checked = false;
    const QMetaObject::Connection check =
        QObject::connect(&tab(), &DeviceTabViewModel::sessionChanged, &tab(), [&] {
            if (old) {
                // The old session still exists here: its signals must not reach the console.
                old->linesDropped(7); // signal
                checked = true;
            }
        });
    tab().disconnectDevice();
    QObject::disconnect(check);
    EXPECT_TRUE(checked);
    EXPECT_EQ(console().droppedLines(), 0U);
    console().refreshNow();
    const int shellRows = console().shell()->rowCount();
    EXPECT_TRUE(rows(*console().shell()).contains(QStringLiteral("— disconnected —")));
    EXPECT_FALSE(console().showConnectState()); // lines are there

    // Connect again: the earlier lines stay (ADR-0063).
    ASSERT_TRUE(connectAndWait(tab()));
    ASSERT_TRUE(waitFor([&] { return console().shell()->rowCount() > shellRows + 1; }));
    EXPECT_GE(rows(*console().shell()).count(QStringLiteral("uart:~$ booted A")), 2);
}

TEST_F(ConsoleViewModelTest, SendsAndSharesTheHistoryBetweenTabs) {
    ASSERT_TRUE(connectAndWait(tab()));
    console().send(QStringLiteral("version"));
    ASSERT_TRUE(waitFor([&] {
        return rows(*console().shell()).contains(QStringLiteral("Firmware version: v3.1.0"));
    }));
    EXPECT_TRUE(rows(*console().shell()).contains(QStringLiteral("> version")));

    // A second tab with the same profile/target sees the command (ADR-0058, shared per pair).
    app().newTab();
    DeviceTabViewModel* second = app().currentTabViewModel();
    ASSERT_NE(second, &tab());
    ASSERT_TRUE(second->configure(QStringLiteral("chester"), QStringLiteral("app"), 801000002));
    ConsoleViewModel* other = consoleOf(*second);
    EXPECT_EQ(other->historyPrevious(QStringLiteral("draft")), QStringLiteral("version"));
    EXPECT_EQ(other->historyNext(QStringLiteral("version")), QStringLiteral("draft"));
    EXPECT_EQ(other->historySearch(QStringLiteral("VERS")), QStringList{QStringLiteral("version")});
    // Another target has another history.
    ASSERT_TRUE(second->configure(QStringLiteral("chester"), QStringLiteral("lte"), 801000002));
    EXPECT_TRUE(other->historySearch(QString()).isEmpty());
}

TEST_F(ConsoleViewModelTest, HistoryUpAndDown) {
    ASSERT_TRUE(connectAndWait(tab()));
    for (const char* command : {"a", "b", "c", "b"}) {
        console().send(QString::fromLatin1(command));
    }
    EXPECT_EQ(console().historyPrevious(QStringLiteral("typed")), QStringLiteral("b"));
    EXPECT_EQ(console().historyPrevious(QString()), QStringLiteral("c"));
    EXPECT_EQ(console().historyPrevious(QString()), QStringLiteral("b"));
    EXPECT_EQ(console().historyPrevious(QString()), QStringLiteral("a"));
    EXPECT_EQ(console().historyPrevious(QString()), QStringLiteral("a")); // the oldest stays
    EXPECT_EQ(console().historyNext(QStringLiteral("a")), QStringLiteral("b"));
    EXPECT_EQ(console().historyNext(QStringLiteral("b")), QStringLiteral("c"));
    EXPECT_EQ(console().historyNext(QStringLiteral("c")), QStringLiteral("b"));
    EXPECT_EQ(console().historyNext(QStringLiteral("b")), QStringLiteral("typed")); // the draft
    // Search: newest first, unique.
    EXPECT_EQ(console().historySearch(QString()),
              (QStringList{QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("a")}));
    EXPECT_EQ(console().historySearch(QString(), 2).size(), 2);
}

// Down while not walking the history keeps the typed text; while walking it goes forward and
// then back to the text typed before the walk (user decision 2026-10-16).
TEST_F(ConsoleViewModelTest, DownKeepsTheTypedTextWhenNotNavigating) {
    EXPECT_EQ(console().historyNext(QStringLiteral("typed")),
              QStringLiteral("typed")); // no history
    ASSERT_TRUE(connectAndWait(tab()));
    ASSERT_TRUE(console().send(QStringLiteral("one")));
    ASSERT_TRUE(console().send(QStringLiteral("two")));
    EXPECT_EQ(console().historyNext(QStringLiteral("abc")), QStringLiteral("abc"));
    EXPECT_EQ(console().historyNext(QStringLiteral("abc")), QStringLiteral("abc"));
    EXPECT_EQ(console().historyPrevious(QStringLiteral("abc")), QStringLiteral("two"));
    EXPECT_EQ(console().historyPrevious(QStringLiteral("two")), QStringLiteral("one"));
    EXPECT_EQ(console().historyNext(QStringLiteral("one")), QStringLiteral("two"));
    EXPECT_EQ(console().historyNext(QStringLiteral("two")), QStringLiteral("abc")); // the draft
    EXPECT_EQ(console().historyNext(QStringLiteral("abc!")),
              QStringLiteral("abc!")); // done walking
}

TEST_F(ConsoleViewModelTest, HistorySurvivesTheApplication) {
    ASSERT_TRUE(connectAndWait(tab()));
    console().send(QStringLiteral("app config show"));
    const std::filesystem::path file =
        app().historyStore().fileFor(QStringLiteral("chester"), QStringLiteral("app"));
    // The CLI's file name in the console directory.
    EXPECT_EQ(file.filename(), "history-chester-app.txt");
    TearDown();
    SetUp();
    EXPECT_EQ(console().historyPrevious(QString()), QStringLiteral("app config show"));
}

TEST_F(ConsoleViewModelTest, InputStatesAndBusy) {
    ASSERT_TRUE(connectAndWait(tab()));
    EXPECT_TRUE(console().inputEnabled());
    EXPECT_TRUE(console().canReset());
    auto* page = qobject_cast<TestPageViewModel*>(tab().pageViewModel(TestPageViewModel::pageId));
    ASSERT_NE(page, nullptr);
    page->startWork(); // "Working…"
    EXPECT_FALSE(console().inputEnabled());
    EXPECT_FALSE(console().canReset());
    EXPECT_FALSE(console().canReconnect());
    EXPECT_FALSE(console().canRunBatch());
    EXPECT_EQ(console().inputDisabledReason(),
              QStringLiteral("Working — the console resumes automatically"));
    page->finishWork();
    ASSERT_TRUE(waitFor([&] { return console().inputEnabled(); }));
    // The shell's markers, shown by the console (review S5).
    console().refreshNow();
    const QStringList shell = rows(*console().shell());
    EXPECT_TRUE(shell.contains(QStringLiteral("— paused: Working… —")));
    EXPECT_TRUE(shell.contains(QStringLiteral("— resumed —")));
}

TEST_F(ConsoleViewModelTest, BatchSendsThePacedCommands) {
    ASSERT_TRUE(connectAndWait(tab()));
    console().batchRunner().setInterval(std::chrono::milliseconds(20));
    const auto file = writeFile("batch.txt", "# comment\nversion\n\n  \r\nversion\r\n# end\n");
    console().runBatch(url(file));
    EXPECT_TRUE(console().batchRunning());
    EXPECT_EQ(console().batchTotal(), 2);
    EXPECT_FALSE(console().canRunBatch());
    ASSERT_TRUE(waitFor([&] { return !console().batchRunning(); }));
    EXPECT_EQ(console().batchDone(), 2);
    EXPECT_FALSE(console().error().valid());
    ASSERT_TRUE(waitFor([&] {
        return rows(*console().shell()).count(QStringLiteral("Firmware version: v3.1.0")) == 2;
    }));
}

TEST_F(ConsoleViewModelTest, BatchFileLimits) {
    ASSERT_TRUE(connectAndWait(tab()));
    console().runBatch(url(root() / "missing.txt"));
    EXPECT_EQ(console().error().code(), QStringLiteral("io.read_failed"));
    EXPECT_EQ(console().error().message(), QStringLiteral("The batch file could not be read."));

    std::string many;
    for (int i = 0; i <= BatchRunner::maxCommands; ++i) {
        many += "c\n";
    }
    console().runBatch(url(writeFile("many.txt", many)));
    EXPECT_EQ(console().error().code(), QStringLiteral("usage.invalid_argument"));
    EXPECT_EQ(console().error().message(),
              QStringLiteral("The batch file is too large (max. 10,000 commands)."));

    console().runBatch(url(writeFile("big.txt", std::string(BatchRunner::maxFileBytes + 1, 'x'))));
    EXPECT_EQ(console().error().code(), QStringLiteral("usage.invalid_argument"));
    console().runBatch(url(writeFile("empty.txt", "# only\n\n")));
    EXPECT_EQ(console().error().code(), QStringLiteral("usage.invalid_argument"));
    EXPECT_FALSE(console().batchRunning());
}

TEST(BatchRunner, QueueFullRetriesTheSameCommand) {
    BatchRunner runner;
    runner.setInterval(std::chrono::milliseconds(5));
    std::vector<std::string> sent;
    int calls = 0;
    std::optional<hio::Error> result;
    bool finished = false;
    QObject::connect(&runner, &BatchRunner::finished, &runner,
                     [&](const std::optional<hio::Error>& error) {
                         result = error;
                         finished = true;
                     });
    runner.start(QStringList{QStringLiteral("a"), QStringLiteral("b")},
                 [&](const std::string& command) -> hio::Status {
                     ++calls;
                     if (calls == 2) { // "b" the first time
                         return std::unexpected(hio::makeError(ErrorCode::InternalQueueFull));
                     }
                     sent.push_back(command);
                     return {};
                 });
    ASSERT_TRUE(waitFor([&] { return finished; }));
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(sent, (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(calls, 3);

    // Another error ends the batch with it.
    finished = false;
    runner.start(QStringList{QStringLiteral("x"), QStringLiteral("y")},
                 [](const std::string&) -> hio::Status {
                     return std::unexpected(hio::makeError(ErrorCode::Cancelled));
                 });
    ASSERT_TRUE(waitFor([&] { return finished; }));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value_or(hio::Error{}).code, ErrorCode::Cancelled);
    EXPECT_EQ(runner.done(), 0);
}

TEST_F(ConsoleViewModelTest, BatchStopsOnDisconnect) {
    ASSERT_TRUE(connectAndWait(tab()));
    console().batchRunner().setInterval(std::chrono::milliseconds(10'000));
    console().runBatch(url(writeFile("slow.txt", "version\nversion\nversion\n")));
    ASSERT_TRUE(console().batchRunning());
    EXPECT_EQ(console().batchDone(), 1); // the first command is sent at once
    tab().disconnectDevice();
    EXPECT_FALSE(console().batchRunning());
    EXPECT_FALSE(console().error().valid()); // a session change stops it quietly
}

TEST_F(ConsoleViewModelTest, BatchStopsWhenTheDeviceDisconnects) {
    // RTT reads fail from 1.5 s on: the session reports Disconnected (auto-reconnect retries).
    useScenario(R"({"probes": [{"serialNumber": 801000001, "product": "Test Probe",
        "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted A\n"}],
                "readErrors": [[1500, 600000]]}}]})");
    ASSERT_TRUE(connectAndWait(tab()));
    console().batchRunner().setInterval(std::chrono::milliseconds(10'000));
    console().runBatch(url(writeFile("slow2.txt", "version\nversion\n")));
    ASSERT_TRUE(console().batchRunning());
    ASSERT_TRUE(waitFor([&] { return !console().batchRunning(); }));
    EXPECT_EQ(console().error().code(), QStringLiteral("target.not_connected"));
    EXPECT_EQ(console().error().message(),
              QStringLiteral("The batch stopped after 1 of 2 commands."));
}

TEST_F(ConsoleViewModelTest, ResetUsesTheSessionOperation) {
    // Without a session: ensureSession() first, then the reset (ADR-0062).
    EXPECT_TRUE(console().canReset());
    console().reset(false);
    ASSERT_NE(tab().session(), nullptr);
    EXPECT_FALSE(console().canReset()); // one reset at a time
    ASSERT_TRUE(waitFor([&] { return console().canReset(); }));
    EXPECT_FALSE(console().error().valid()) << console().error().message().toStdString();
    EXPECT_TRUE(app().toasts().contains(QStringLiteral("The device was reset.")));
    ASSERT_TRUE(waitFor([&] { return tab().state() == DeviceTabViewModel::Connected; }));
    console().reset(true);
    ASSERT_TRUE(waitFor([&] { return console().canReset(); }));
    EXPECT_FALSE(console().error().valid()) << console().error().message().toStdString();
}

TEST_F(ConsoleViewModelTest, ClearSaveAndCopy) {
    ASSERT_TRUE(connectAndWait(tab()));
    ASSERT_TRUE(waitFor(
        [&] { return console().log()->rowCount() >= 1 && console().shell()->rowCount() >= 2; }));
    const auto saved = root() / "log.txt";
    console().saveView(1, url(saved));
    EXPECT_FALSE(console().error().valid());
    QFile file(hio::toQString(hio::toUtf8(saved)));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_TRUE(file.readAll().contains("<inf> app: started"));
    console().saveView(1, url(root() / "missing-dir" / "x" / "log.txt"));
    EXPECT_EQ(console().error().code(), QStringLiteral("io.write_failed"));
    console().dismissError();

    console().clear(1); // shell only
    EXPECT_EQ(console().shell()->rowCount(), 0);
    EXPECT_GE(console().log()->rowCount(), 1);
    EXPECT_EQ(tab().consoleLog()->size(Channel::Shell), 0U);
    console().clear(3);
    EXPECT_EQ(console().log()->rowCount(), 0);
}

// Plan 0005 decision 2026-10-16 (review K10): Open log file opens the session's console file
// (DeviceSession::consoleFile()), and the last one after Disconnect.
TEST_F(ConsoleViewModelTest, OpensTheSessionsConsoleFile) {
    QUrl opened;
    console().settings()->setUrlOpener([&](const QUrl& target) {
        opened = target;
        return true;
    });
    console().openLogFile(); // no session yet
    EXPECT_TRUE(opened.isEmpty());
    EXPECT_TRUE(app().toasts().contains(
        QStringLiteral("There is no console log file for this device yet.")));

    // A file of the same probe that is newer than the session's is not taken (no heuristic).
    const std::filesystem::path folder = console().settings()->logFolderPath();
    EXPECT_EQ(folder, registryOptions().consoleDirectory);
    ASSERT_TRUE(connectAndWait(tab()));
    ASSERT_TRUE(waitFor([&] { return !console().consoleFile().empty(); }));
    const std::filesystem::path file = console().consoleFile();
    EXPECT_EQ(file, tab().session()->consoleFile());
    EXPECT_EQ(file.parent_path(), folder);
    std::ofstream(folder / "chester-app-29991231-235959-801000001.log") << "not this one";
    console().openLogFile();
    EXPECT_EQ(opened, url(file));

    tab().disconnectDevice();
    ASSERT_EQ(tab().session(), nullptr);
    EXPECT_EQ(console().consoleFile(), file); // the last one of this tab
    opened.clear();
    console().openLogFile();
    EXPECT_EQ(opened, url(file));
    console().openLogFolder();
    EXPECT_EQ(opened, url(folder));
}

// Review K8: another profile / target resets the history walk (index, revision, draft); the
// first Up after a restart starts from the newest entry.
TEST_F(ConsoleViewModelTest, HistoryWalkStartsAgainAfterAConfigurationChange) {
    ASSERT_TRUE(connectAndWait(tab()));
    ASSERT_TRUE(console().send(QStringLiteral("first")));
    ASSERT_TRUE(console().send(QStringLiteral("second")));
    EXPECT_EQ(console().historyPrevious(QStringLiteral("draft")), QStringLiteral("second"));
    tab().disconnectDevice();
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("lte"), 801000001));
    EXPECT_EQ(console().historyNext(QStringLiteral("x")), QStringLiteral("x")); // no old draft
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    EXPECT_EQ(console().historyPrevious(QStringLiteral("typed")), QStringLiteral("second"));
    EXPECT_EQ(console().historyNext(QStringLiteral("second")), QStringLiteral("typed"));

    TearDown(); // the history file is loaded by a new application
    SetUp();
    EXPECT_EQ(console().historyPrevious(QString()), QStringLiteral("second"));
    EXPECT_EQ(console().historyPrevious(QString()), QStringLiteral("first"));
}

// Review K9: a command that was not sent stays in the input; a batch stopped by another page's
// operation says so.
TEST_F(ConsoleViewModelTest, SendReportsWhetherTheCommandWasSent) {
    EXPECT_FALSE(console().send(QStringLiteral("version"))); // not connected
    ASSERT_TRUE(connectAndWait(tab()));
    EXPECT_TRUE(console().send(QStringLiteral("version")));
}

TEST_F(ConsoleViewModelTest, BatchStoppedByBusySaysSo) {
    ASSERT_TRUE(connectAndWait(tab()));
    console().batchRunner().setInterval(std::chrono::milliseconds(10'000));
    console().runBatch(url(writeFile("busy.txt", "version\nversion\nversion\n")));
    ASSERT_TRUE(console().batchRunning());
    auto* page = qobject_cast<TestPageViewModel*>(tab().pageViewModel(TestPageViewModel::pageId));
    ASSERT_NE(page, nullptr);
    page->startWork();
    EXPECT_FALSE(console().batchRunning());
    EXPECT_EQ(console().error().code(), QStringLiteral("internal.busy"));
    EXPECT_EQ(console().error().message(),
              QStringLiteral("The batch stopped after 1 of 3 commands."));
    EXPECT_EQ(console().error().cause(),
              QStringLiteral("another operation started on the device."));
    page->finishWork();
}

// Review K12: a language switch re-reads the translated properties (AppViewModel's signal).
TEST_F(ConsoleViewModelTest, LanguageSwitchUpdatesTheTexts) {
    const QSignalSpy changed(&console(), &ConsoleViewModel::stateChanged);
    app().languageChanged(); // signal, as the language manager's switch forwards it
    EXPECT_GE(changed.count(), 1);
}

TEST_F(ConsoleViewModelTest, StackedBelow1000PxOrByChoice) {
    console().setPageWidth(1200);
    EXPECT_FALSE(console().stackedLayout());
    console().setPageWidth(999);
    EXPECT_TRUE(console().stackedLayout());
    console().setPageWidth(1200);
    console().settings()->setStacked(true);
    EXPECT_TRUE(console().stackedLayout());
    EXPECT_TRUE(app().settingsStore()->value(QStringLiteral("console/stacked")).toBool());
    console().settings()->setSplitRatio(95); // clamped
    EXPECT_EQ(console().settings()->splitRatio(), 80);
    console().settings()->setFontSize(30);
    EXPECT_EQ(console().settings()->fontSize(), 24);
    console().settings()->resetFontSize();
    EXPECT_EQ(console().settings()->fontSize(), 10);
}

} // namespace
