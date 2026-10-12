// ConnectionViewModel: the state text for every ConnectionEvent, options from the settings and
// the profile, per-probe memory with the LRU bound (plan 0004, B4; ADR-0052).

#include "gui_test_support.h"
#include "hio/core/error.h"
#include "hio/device/session_events.h"
#include "settings_store.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"

#include <QSettings>
#include <QString>
#include <QStringList>
#include <QtContainerFwd>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>

namespace {

using hio::ErrorCode;
using hio::device::ConnectionEvent;
using hio::device::ConnectionState;
using hio::gui::AppViewModel;
using hio::gui::ConnectionViewModel;
using hio::gui::DeviceTabViewModel;

class ConnectionTest : public hio::gui::test::GuiTest {
protected:
    void SetUp() override {
        GuiTest::SetUp();
        m_app = makeApp();
    }
    void TearDown() override {
        m_app.reset();
        GuiTest::TearDown();
    }
    [[nodiscard]] AppViewModel& app() { return *m_app; }
    [[nodiscard]] DeviceTabViewModel& tab() { return *m_app->currentTabViewModel(); }
    [[nodiscard]] ConnectionViewModel& connection() { return *tab().connection(); }

private:
    std::unique_ptr<AppViewModel> m_app;
};

TEST_F(ConnectionTest, StateTextForEveryConnectionEvent) {
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 683012345,
                                QStringLiteral("J-Link EDU Mini")));
    ConnectionViewModel& vm = connection();
    EXPECT_EQ(vm.probeLabel(), QStringLiteral("J-Link EDU Mini · 683012345"));
    EXPECT_EQ(vm.stateText(), QStringLiteral("Disconnected"));
    EXPECT_EQ(vm.stateKind(), QStringLiteral("idle"));
    EXPECT_EQ(vm.action(), QStringLiteral("connect"));
    EXPECT_TRUE(std::isnan(vm.targetVolts()));

    vm.setConnection(ConnectionEvent{.state = ConnectionState::Connecting, .error = std::nullopt});
    EXPECT_EQ(vm.stateText(), QStringLiteral("Connecting…"));
    EXPECT_EQ(vm.stateKind(), QStringLiteral("connecting"));
    EXPECT_EQ(vm.action(), QStringLiteral("cancel"));

    vm.setConnection(ConnectionEvent{.state = ConnectionState::Connected, .error = std::nullopt});
    EXPECT_EQ(vm.stateText(), QStringLiteral("Connected"));
    EXPECT_EQ(vm.stateKind(), QStringLiteral("connected"));
    EXPECT_EQ(vm.action(), QStringLiteral("disconnect"));
    EXPECT_FALSE(vm.error().valid());

    vm.setConnection(ConnectionEvent{.state = ConnectionState::Disconnected,
                                     .error = hio::makeError(ErrorCode::TargetNoPower)});
    EXPECT_EQ(vm.stateText(), QStringLiteral("Reconnecting… (target not powered)"));
    EXPECT_EQ(vm.stateKind(), QStringLiteral("reconnecting"));
    EXPECT_EQ(vm.action(), QStringLiteral("disconnect"));
    EXPECT_EQ(vm.error().code(), QStringLiteral("target.no_power"));

    vm.setConnection(
        ConnectionEvent{.state = ConnectionState::Disconnected, .error = std::nullopt});
    EXPECT_EQ(vm.stateText(), QStringLiteral("Reconnecting…"));

    vm.setConnection(ConnectionEvent{.state = ConnectionState::Stopped,
                                     .error = hio::makeError(ErrorCode::TargetConnectFailed)});
    EXPECT_EQ(vm.stateText(), QStringLiteral("Disconnected — target does not respond"));
    EXPECT_EQ(vm.stateKind(), QStringLiteral("error"));
    EXPECT_EQ(vm.action(), QStringLiteral("connect"));

    vm.setConnection(ConnectionEvent{.state = ConnectionState::Stopped, .error = std::nullopt});
    EXPECT_EQ(vm.stateText(), QStringLiteral("Disconnected"));
    EXPECT_EQ(vm.stateKind(), QStringLiteral("idle"));

    vm.setConnection(ConnectionEvent{.state = ConnectionState::Connected, .error = std::nullopt});
    vm.setConnection(std::nullopt);
    EXPECT_EQ(vm.stateText(), QStringLiteral("Disconnected"));

    // An error without a session (open() failed), then dismissed.
    vm.setOpenError(hio::makeError(ErrorCode::ProbeInUseByOtherProcess));
    EXPECT_EQ(vm.stateText(), QStringLiteral("Disconnected — J-Link used by another program"));
    EXPECT_EQ(vm.stateKind(), QStringLiteral("error"));
    EXPECT_TRUE(tab().hasError() || vm.error().valid());
    vm.triggerErrorAction(hio::gui::error_action::Dismiss);
    EXPECT_FALSE(vm.error().valid());
    EXPECT_EQ(vm.stateText(), QStringLiteral("Disconnected"));
}

TEST_F(ConnectionTest, ShortCauses) {
    EXPECT_EQ(ConnectionViewModel::shortCause(hio::makeError(ErrorCode::ProbeNotFound)),
              QStringLiteral("J-Link not found"));
    EXPECT_EQ(ConnectionViewModel::shortCause(hio::makeError(ErrorCode::TargetRttNotFound)),
              QStringLiteral("no RTT console found"));
    EXPECT_EQ(ConnectionViewModel::shortCause(hio::makeError(ErrorCode::DependencyJLinkTooOld)),
              QStringLiteral("J-Link Software missing"));
    const hio::Error other = hio::makeError(ErrorCode::IoReadFailed);
    EXPECT_EQ(ConnectionViewModel::shortCause(other), QString::fromStdString(other.message));
}

TEST_F(ConnectionTest, OptionsComeFromTheProfileAndTheSettings) {
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 683012345));
    EXPECT_EQ(connection().speedKhz(), 2000); // CHESTER app default
    EXPECT_EQ(connection().latencyMs(), 50);
    EXPECT_TRUE(connection().autoReconnect());
    EXPECT_FALSE(connection().resetOnOpen());
    EXPECT_TRUE(connection().logToFile());

    ASSERT_TRUE(app().settingsStore()->setValue(QStringLiteral("jlink/speedKhz"), 1000));
    ASSERT_TRUE(app().settingsStore()->setValue(QStringLiteral("rtt/autoReconnect"), false));
    app().newTab();
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 683012346));
    EXPECT_EQ(connection().speedKhz(), 1000);
    EXPECT_EQ(connection().latencyMs(), 50);
    EXPECT_FALSE(connection().autoReconnect());
}

TEST_F(ConnectionTest, OptionsAreRememberedPerProbe) {
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 683012345));
    connection().setSpeedKhz(4000);
    connection().setResetOnOpen(true);
    connection().setSpeedKhz(1); // out of range: ignored
    connection().setLatencyMs(5000);
    EXPECT_EQ(connection().speedKhz(), 4000);
    EXPECT_EQ(connection().latencyMs(), 50);
    EXPECT_EQ(app().settingsStore()->value(QStringLiteral("probes/683012345/speedKhz")).toInt(),
              4000);
    EXPECT_EQ(app().settingsStore()->value(QStringLiteral("probes/recent")).toString(),
              QStringLiteral("683012345"));

    // Another tab with the same probe gets the remembered values; another probe the defaults.
    app().newTab();
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 683012345));
    EXPECT_EQ(connection().speedKhz(), 4000);
    EXPECT_TRUE(connection().resetOnOpen());
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 1));
    EXPECT_EQ(connection().speedKhz(), 2000);
    EXPECT_FALSE(connection().resetOnOpen());
}

TEST_F(ConnectionTest, AtMost32ProbesAreRemembered) {
    for (int serial = 1; serial <= 40; ++serial) {
        ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"),
                                    static_cast<std::uint32_t>(serial)));
        connection().setSpeedKhz(1000 + serial);
    }
    const QStringList recent = app()
                                   .settingsStore()
                                   ->value(QStringLiteral("probes/recent"))
                                   .toString()
                                   .split(QLatin1Char(','));
    ASSERT_EQ(recent.size(), 32);
    EXPECT_EQ(recent.front(), QStringLiteral("40"));
    EXPECT_EQ(recent.back(), QStringLiteral("9"));
    // A forgotten probe gets the defaults again.
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 3));
    EXPECT_EQ(connection().speedKhz(), 2000);
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 39));
    EXPECT_EQ(connection().speedKhz(), 1039);
}

} // namespace
