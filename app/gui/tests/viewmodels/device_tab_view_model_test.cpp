// DeviceTabViewModel against the fake J-Link library: session ownership, ensureSession, the tab's
// ConsoleLog across sessions (ADR-0063), Connecting -> Cancel, restored tab with a missing probe,
// busy API with close and quit confirmation, page view models (plan 0004, B2).

#include "gui_test_support.h"
#include "hio/core/error.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/device_session.h"
#include "hio/device/session_events.h"
#include "support/test_page_view_model.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/device_tab_list_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"

#include <QObject>
#include <QPointer>
#include <QSignalSpy>
#include <QString>
#include <QtContainerFwd>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace {

using hio::ErrorCode;
using hio::device::Channel;
using hio::device::ConnectionState;
using hio::device::Direction;
using hio::gui::AppViewModel;
using hio::gui::DeviceTabViewModel;
using hio::gui::test::spin;
using hio::gui::test::TestPageViewModel;
using hio::gui::test::waitFor;

class DeviceTabTest : public hio::gui::test::GuiTest {
protected:
    void SetUp() override {
        GuiTest::SetUp();
        m_app = makeApp();
        m_tab = m_app->currentTabViewModel();
    }
    void TearDown() override {
        m_app.reset();
        GuiTest::TearDown();
    }

    [[nodiscard]] AppViewModel& app() { return *m_app; }
    [[nodiscard]] DeviceTabViewModel& tab() { return *m_tab; }
    [[nodiscard]] TestPageViewModel* testPage() {
        return qobject_cast<TestPageViewModel*>(m_tab->pageViewModel(TestPageViewModel::pageId));
    }

    [[nodiscard]] static std::vector<std::string> markers(const hio::device::ConsoleLog& log) {
        std::vector<std::string> texts;
        for (const auto& line : log.linesSince(0, hio::device::ChannelMask::Shell)) {
            if (line.dir == Direction::Marker) {
                texts.push_back(line.text);
            }
        }
        return texts;
    }

    static bool connectAndWait(DeviceTabViewModel& tab) {
        tab.connectDevice();
        return waitFor([&] { return tab.state() == DeviceTabViewModel::Connected; });
    }

private:
    std::unique_ptr<AppViewModel> m_app;
    DeviceTabViewModel* m_tab = nullptr;
};

TEST_F(DeviceTabTest, PageViewModelsInRegistryOrder) {
    const QObjectList pages = tab().pageViewModels();
    ASSERT_EQ(pages.size(), 3); // console, flash, test (settings is global)
    for (QObject* page : pages) {
        ASSERT_NE(page, nullptr);
        EXPECT_EQ(page->parent(), &tab());
    }
    EXPECT_NE(testPage(), nullptr);
    EXPECT_EQ(tab().currentPage(), QStringLiteral("console"));
    tab().setCurrentPage(QStringLiteral("settings")); // global: not a page of the tab
    EXPECT_EQ(tab().currentPage(), QStringLiteral("console"));
    tab().setCurrentPage(QStringLiteral("flash"));
    EXPECT_EQ(tab().currentPage(), QStringLiteral("flash"));
}

TEST_F(DeviceTabTest, ConnectDisconnectKeepsTheLog) {
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001,
                                QStringLiteral("Test Probe")));
    EXPECT_EQ(tab().state(), DeviceTabViewModel::Disconnected);
    QSignalSpy sessions(&tab(), &DeviceTabViewModel::sessionChanged);
    const auto log = tab().consoleLog();

    ASSERT_TRUE(connectAndWait(tab()));
    EXPECT_EQ(sessions.count(), 1);
    ASSERT_NE(tab().session(), nullptr);
    EXPECT_EQ(testPage()->sessionChanges(), 1);
    EXPECT_EQ(&tab().session()->log(), log.get()); // the tab's log (ADR-0063)
    ASSERT_TRUE(waitFor([&] { return log->size(Channel::Shell) >= 2; }));
    // "Connected · 3.30 V" (the fake probe's VTref, ADR-0091).
    EXPECT_TRUE(tab().connection()->stateText().startsWith(QStringLiteral("Connected")));
    EXPECT_EQ(tab().connection()->action(), QStringLiteral("disconnect"));

    tab().disconnectDevice();
    EXPECT_EQ(tab().session(), nullptr);
    EXPECT_EQ(tab().state(), DeviceTabViewModel::Disconnected);
    EXPECT_EQ(sessions.count(), 2);
    EXPECT_EQ(app().sessionRegistry().sessionCount(), 0U);
    const std::size_t linesBefore = log->size(Channel::Shell);
    EXPECT_GE(linesBefore, 2U);

    // Connect again and then change the probe: the same log, the old lines stay.
    ASSERT_TRUE(connectAndWait(tab()));
    EXPECT_EQ(tab().consoleLog(), log);
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 801000002));
    EXPECT_EQ(tab().session(), nullptr);
    ASSERT_TRUE(connectAndWait(tab()));
    EXPECT_EQ(tab().consoleLog(), log);
    EXPECT_GT(log->size(Channel::Shell), linesBefore);
    const auto texts = markers(*log);
    ASSERT_GE(texts.size(), 3U);
    EXPECT_EQ(texts.front(), "— connected —");
    EXPECT_EQ(texts[1], "— disconnected —");
    spin(std::chrono::milliseconds(20));
}

TEST_F(DeviceTabTest, ConnectingCanBeCancelled) {
    useScenario(R"({"probes": [{"serialNumber": 9, "connectDelayMs": 1500}]})");
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 9));
    const auto start = std::chrono::steady_clock::now();
    tab().connectDevice();
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(500));
    EXPECT_EQ(tab().state(), DeviceTabViewModel::Connecting);
    EXPECT_EQ(tab().connection()->stateText(), QStringLiteral("Connecting…"));
    EXPECT_EQ(tab().connection()->action(), QStringLiteral("cancel"));
    tab().connection()->triggerAction(); // Cancel = close()
    EXPECT_EQ(tab().session(), nullptr);
    EXPECT_EQ(tab().state(), DeviceTabViewModel::Disconnected);
    EXPECT_EQ(tab().connection()->stateText(), QStringLiteral("Disconnected"));
    EXPECT_EQ(app().sessionRegistry().sessionCount(), 0U);
}

TEST_F(DeviceTabTest, MissingProbeOffersAnotherProbe) {
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 123456));
    tab().connectDevice();
    ASSERT_TRUE(waitFor([&] { return tab().connection()->error().valid(); }));
    EXPECT_EQ(tab().connection()->error().code(), QStringLiteral("probe.not_found"));
    EXPECT_TRUE(tab().hasError());
    EXPECT_EQ(tab().state(), DeviceTabViewModel::Disconnected);
    EXPECT_TRUE(tab().connection()->canChooseAnotherProbe());
    EXPECT_EQ(tab().connection()->stateKind(), QStringLiteral("error"));
    EXPECT_TRUE(tab().connection()->stateText().startsWith(QStringLiteral("Disconnected — ")));
    tab().connection()->chooseAnotherProbe();
    EXPECT_EQ(tab().state(), DeviceTabViewModel::ChoosingDevice);
    EXPECT_EQ(tab().session(), nullptr);
}

TEST_F(DeviceTabTest, MissingLibraryIsADependencyError) {
    qputenv("HIO_JLINK_LIBRARY", "/nonexistent/libjlinkarm.so");
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    tab().connectDevice();
    EXPECT_EQ(tab().session(), nullptr);
    ASSERT_TRUE(tab().connection()->error().valid());
    EXPECT_EQ(tab().connection()->error().code(), QStringLiteral("dependency.jlink_not_found"));
    EXPECT_EQ(tab().connection()->error().primaryAction(), hio::gui::error_action::OpenSettings);
}

TEST_F(DeviceTabTest, EnsureSessionOpensTheConsoleTarget) {
    // No probe chosen yet.
    const auto none = tab().ensureSession();
    ASSERT_FALSE(none);
    EXPECT_EQ(none.error().code, ErrorCode::UsageInvalidArgument);

    // Review S2: always the console target with RTT (operations carry their own target).
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    ASSERT_TRUE(tab().ensureSession());
    hio::device::DeviceSession* session = tab().session();
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->target().id, "app");
    ASSERT_TRUE(tab().ensureSession()); // no-op: a session exists
    EXPECT_EQ(tab().session(), session);
    ASSERT_TRUE(waitFor([&] { return tab().state() == DeviceTabViewModel::Connected; }));
    // RTT is attached: the console shows the device's output.
    ASSERT_TRUE(waitFor([&] { return tab().consoleLog()->size(Channel::Shell) >= 2; }));
    // Busy: refused.
    testPage()->startWork();
    EXPECT_EQ(tab().ensureSession().error().code, ErrorCode::InternalBusy);
    testPage()->finishWork();
}

TEST_F(DeviceTabTest, AStoppedSessionIsReplaced) {
    // Review S1: a fatal connect failure (missing probe) keeps a stopped session; the target can
    // be changed and ensureSession() opens a new session.
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 123456));
    tab().connectDevice();
    ASSERT_TRUE(waitFor([&] { return tab().connection()->error().valid(); }));
    ASSERT_NE(tab().session(), nullptr);
    EXPECT_TRUE(tab().connection()->canChangeTarget());
    EXPECT_EQ(tab().connection()->action(), QStringLiteral("connect"));
    tab().connection()->setTargetId(QStringLiteral("lte"));
    EXPECT_EQ(tab().consoleTargetId(), QStringLiteral("lte"));
    EXPECT_EQ(tab().session(), nullptr); // the stopped session was released
    tab().connection()->setTargetId(QStringLiteral("app"));
    tab().connectDevice();
    ASSERT_TRUE(waitFor([&] { return tab().connection()->error().valid(); }));
    const hio::device::DeviceSession* stopped = tab().session();
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 123456));
    EXPECT_TRUE(tab().ensureSession()); // replaces the stopped session
    EXPECT_NE(tab().session(), nullptr);
    EXPECT_NE(tab().session(), stopped);
    spin(std::chrono::milliseconds(20));
}

TEST_F(DeviceTabTest, ShutdownTellsThePages) {
    // Review S3: pages hear sessionChanged before the session is deleted.
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    ASSERT_TRUE(connectAndWait(tab()));
    QSignalSpy sessions(&tab(), &DeviceTabViewModel::sessionChanged);
    bool sawNull = false;
    QObject::connect(&tab(), &DeviceTabViewModel::sessionChanged, &tab(),
                     [&] { sawNull = tab().session() == nullptr; });
    tab().shutdown();
    EXPECT_EQ(sessions.count(), 1);
    EXPECT_TRUE(sawNull);
    EXPECT_EQ(app().sessionRegistry().sessionCount(), 0U);
}

TEST_F(DeviceTabTest, DismissedErrorClearsTheTabError) {
    // Review S10.
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 123456));
    tab().connectDevice();
    ASSERT_TRUE(waitFor([&] { return tab().hasError(); }));
    EXPECT_NE(tab().connection()->stateText(), QStringLiteral("Disconnected"));
    tab().connection()->dismissError();
    EXPECT_FALSE(tab().hasError());
    EXPECT_EQ(tab().connection()->stateText(), QStringLiteral("Disconnected"));
    EXPECT_EQ(tab().connection()->stateKind(), QStringLiteral("idle"));
}

TEST_F(DeviceTabTest, TargetChangesOnlyWithoutASession) {
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    tab().connection()->setTargetId(QStringLiteral("lte"));
    EXPECT_EQ(tab().consoleTargetId(), QStringLiteral("lte"));
    EXPECT_EQ(tab().title(), QStringLiteral("CHESTER lte · 801000001"));
    tab().connection()->setTargetId(QStringLiteral("app"));
    ASSERT_TRUE(connectAndWait(tab()));
    EXPECT_FALSE(tab().connection()->canChangeTarget());
    tab().connection()->setTargetId(QStringLiteral("lte"));
    EXPECT_EQ(tab().consoleTargetId(), QStringLiteral("app"));
}

TEST_F(DeviceTabTest, BusyStateAndText) {
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    QSignalSpy busy(&tab(), &DeviceTabViewModel::busyChanged);
    testPage()->startWork();
    EXPECT_EQ(tab().state(), DeviceTabViewModel::Busy);
    EXPECT_EQ(tab().busyPage(), QStringLiteral("test"));
    EXPECT_EQ(tab().busyText(), QStringLiteral("Working…"));
    EXPECT_EQ(tab().connection()->stateText(), QStringLiteral("Working…"));
    EXPECT_EQ(tab().connection()->stateKind(), QStringLiteral("busy"));
    EXPECT_FALSE(tab().connection()->actionEnabled());
    // Another page cannot become busy at the same time.
    tab().setBusy(QStringLiteral("console"), QStringLiteral("x"), QStringLiteral("y"), {});
    EXPECT_EQ(tab().busyPage(), QStringLiteral("test"));
    // Connect is refused while busy.
    tab().connectDevice();
    EXPECT_EQ(tab().session(), nullptr);
    testPage()->finishWork();
    EXPECT_EQ(tab().state(), DeviceTabViewModel::Disconnected);
    EXPECT_TRUE(tab().busyPage().isEmpty());
    EXPECT_EQ(busy.count(), 2);
    // The shell owns the markers (review S5): paused, then resumed.
    const auto texts = markers(*tab().consoleLog());
    ASSERT_GE(texts.size(), 2U);
    EXPECT_EQ(texts[texts.size() - 2], "— paused: Working… —");
    EXPECT_EQ(texts.back(), "— resumed —");
}

TEST_F(DeviceTabTest, CloseConfirmationCancelsAndCloses) {
    app().newTab();
    ASSERT_EQ(app().tabs()->count(), 2);
    QSignalSpy confirm(&app(), &AppViewModel::closeConfirmationNeeded);
    testPage()->startWork();

    app().closeTab(0);
    ASSERT_EQ(confirm.count(), 1);
    // The tab itself (review S6): closing another tab first does not shift the target.
    EXPECT_EQ(confirm.at(0).at(0).value<DeviceTabViewModel*>(), &tab());
    EXPECT_EQ(confirm.at(0).at(1).toString(),
              QStringLiteral("Work is in progress. Cancel it and close the tab?"));
    EXPECT_EQ(app().tabs()->count(), 2); // [Keep running]

    // [Cancel and close]: the page's cancel is called; the tab closes when it clears busy.
    const QPointer<DeviceTabViewModel> busyTab(&tab());
    app().newTab();
    app().closeTab(2); // an idle tab goes away while the dialog would be open
    testPage()->setClearOnCancel(false);
    app().closeTabObject(busyTab.data(), true);
    EXPECT_EQ(testPage()->cancelCalls(), 1);
    spin(std::chrono::milliseconds(50));
    EXPECT_EQ(app().tabs()->count(), 2);
    testPage()->finishWork();
    ASSERT_TRUE(waitFor([&] { return app().tabs()->count() == 1; }));
    ASSERT_TRUE(waitFor([&] { return busyTab.isNull(); }));
}

TEST_F(DeviceTabTest, QuitConfirmationCancelsEveryOperation) {
    QSignalSpy needed(&app(), &AppViewModel::quitConfirmationNeeded);
    QSignalSpy quit(&app(), &AppViewModel::quitRequested);
    testPage()->startWork();
    EXPECT_FALSE(app().canQuit());
    ASSERT_EQ(needed.count(), 1);
    EXPECT_EQ(needed.at(0).at(0).toString(),
              QStringLiteral("Work is in progress. Cancel it and close the tab?"));
    app().cancelOperationsAndQuit();
    EXPECT_EQ(testPage()->cancelCalls(), 1);
    EXPECT_EQ(quit.count(), 1); // the test page clears busy in its cancel callback
    EXPECT_TRUE(app().canQuit());
}

} // namespace
