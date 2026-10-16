// ConnectionViewModel: the state text for every ConnectionEvent, options from the settings and
// the profile, per-probe memory with the LRU bound (plan 0004, B4; ADR-0052).

#include "gui_test_support.h"
#include "hio/core/error.h"
#include "hio/device/device_session.h"
#include "hio/device/session_events.h"
#include "settings_store.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"

#include <QLocale>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QtContainerFwd>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

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
    // Closes the app, lets `edit` change the settings file directly, and starts the app again.
    template <typename Edit> void editSettingsFile(Edit edit) {
        m_app.reset();
        {
            QSettings raw(QString::fromStdU16String(settingsFile().u16string()),
                          QSettings::IniFormat);
            edit(raw);
        }
        m_app = makeApp();
    }

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

    vm.setConnection(ConnectionEvent{.state = ConnectionState::Connecting,
                                     .error = std::nullopt,
                                     .targetMillivolts = std::nullopt});
    EXPECT_EQ(vm.stateText(), QStringLiteral("Connecting…"));
    EXPECT_EQ(vm.stateKind(), QStringLiteral("connecting"));
    EXPECT_EQ(vm.action(), QStringLiteral("cancel"));

    vm.setConnection(ConnectionEvent{.state = ConnectionState::Connected,
                                     .error = std::nullopt,
                                     .targetMillivolts = std::nullopt});
    EXPECT_EQ(vm.stateText(), QStringLiteral("Connected"));
    EXPECT_EQ(vm.stateKind(), QStringLiteral("connected"));
    EXPECT_EQ(vm.action(), QStringLiteral("disconnect"));
    EXPECT_FALSE(vm.error().valid());

    // ADR-0091: the measured target voltage while connected.
    vm.setConnection(ConnectionEvent{
        .state = ConnectionState::Connected, .error = std::nullopt, .targetMillivolts = 3300});
    EXPECT_EQ(vm.stateText(), QStringLiteral("Connected · 3.30 V"));
    EXPECT_DOUBLE_EQ(vm.targetVolts(), 3.3);

    vm.setConnection(ConnectionEvent{.state = ConnectionState::Disconnected,
                                     .error = hio::makeError(ErrorCode::TargetNoPower),
                                     .targetMillivolts = std::nullopt});
    EXPECT_EQ(vm.stateText(), QStringLiteral("Reconnecting… (target not powered)"));
    EXPECT_EQ(vm.stateKind(), QStringLiteral("reconnecting"));
    EXPECT_EQ(vm.action(), QStringLiteral("disconnect"));
    EXPECT_EQ(vm.error().code(), QStringLiteral("target.no_power"));

    vm.setConnection(ConnectionEvent{.state = ConnectionState::Disconnected,
                                     .error = std::nullopt,
                                     .targetMillivolts = std::nullopt});
    EXPECT_EQ(vm.stateText(), QStringLiteral("Reconnecting…"));

    vm.setConnection(ConnectionEvent{.state = ConnectionState::Stopped,
                                     .error = hio::makeError(ErrorCode::TargetConnectFailed),
                                     .targetMillivolts = std::nullopt});
    EXPECT_EQ(vm.stateText(), QStringLiteral("Disconnected — target does not respond"));
    EXPECT_EQ(vm.stateKind(), QStringLiteral("error"));
    EXPECT_EQ(vm.action(), QStringLiteral("connect"));

    vm.setConnection(ConnectionEvent{.state = ConnectionState::Stopped,
                                     .error = std::nullopt,
                                     .targetMillivolts = std::nullopt});
    EXPECT_EQ(vm.stateText(), QStringLiteral("Disconnected"));
    EXPECT_EQ(vm.stateKind(), QStringLiteral("idle"));

    vm.setConnection(ConnectionEvent{.state = ConnectionState::Connected,
                                     .error = std::nullopt,
                                     .targetMillivolts = std::nullopt});
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

TEST_F(ConnectionTest, VoltageFollowsTheLocale) {
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 683012345));
    const QLocale previous;
    QLocale::setDefault(QLocale(QStringLiteral("cs")));
    connection().setConnection(ConnectionEvent{
        .state = ConnectionState::Connected, .error = std::nullopt, .targetMillivolts = 3300});
    EXPECT_EQ(connection().stateText(), QStringLiteral("Connected · 3,30 V"));
    QLocale::setDefault(previous);
    // Not measured: no voltage.
    connection().setConnection(ConnectionEvent{.state = ConnectionState::Connected,
                                               .error = std::nullopt,
                                               .targetMillivolts = std::nullopt});
    EXPECT_EQ(connection().stateText(), QStringLiteral("Connected"));
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
    EXPECT_TRUE(connection().autoSpeed()); // CHESTER app default: automatic (ADR-0090)
    EXPECT_EQ(connection().speedKhz(), 4000);
    EXPECT_EQ(connection().options().sessionSpeedKhz(), std::nullopt);
    EXPECT_EQ(connection().latencyMs(), 50);
    EXPECT_TRUE(connection().autoReconnect());
    EXPECT_FALSE(connection().resetOnOpen());
    EXPECT_TRUE(connection().logToFile());

    ASSERT_TRUE(app().settingsStore()->setValue(QStringLiteral("jlink/speedKhz"), 1000));
    ASSERT_TRUE(app().settingsStore()->setValue(QStringLiteral("rtt/autoReconnect"), false));
    app().newTab();
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 683012346));
    EXPECT_FALSE(connection().autoSpeed());
    EXPECT_EQ(connection().speedKhz(), 1000);
    EXPECT_EQ(connection().options().sessionSpeedKhz(), 1000);
    EXPECT_EQ(connection().latencyMs(), 50);
    EXPECT_FALSE(connection().autoReconnect());
}

TEST_F(ConnectionTest, AutomaticSpeedByDefault) {
    // ADR-0111: on by default; turning it off uses the spin box value (4000 kHz).
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 683012345));
    EXPECT_TRUE(connection().autoSpeed());
    EXPECT_EQ(connection().options().sessionSpeedKhz(), std::nullopt);
    EXPECT_TRUE(connection().speedWarning().isEmpty());

    connection().setAutoSpeed(false);
    EXPECT_FALSE(connection().autoSpeed());
    EXPECT_EQ(connection().options().sessionSpeedKhz(), 4000);
    EXPECT_EQ(app().settingsStore()->value(QStringLiteral("probes/683012345/speedKhz")).toInt(),
              4000);

    // While automatic, the fixed value is kept for later; 0 is stored for automatic.
    connection().setSpeedKhz(12000);
    connection().setAutoSpeed(true);
    EXPECT_EQ(connection().speedKhz(), 12000);
    EXPECT_EQ(connection().options().sessionSpeedKhz(), std::nullopt);
    EXPECT_EQ(app().settingsStore()->value(QStringLiteral("probes/683012345/speedKhz")).toInt(), 0);
    connection().setAutoSpeed(false);
    EXPECT_EQ(connection().options().sessionSpeedKhz(), 12000);
}

TEST_F(ConnectionTest, StoredSpeedZeroIsAutomatic) {
    ASSERT_TRUE(app().settingsStore()->setValue(QStringLiteral("jlink/speedKhz"), 0));
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 683012345));
    EXPECT_TRUE(connection().autoSpeed());
    EXPECT_EQ(connection().options().sessionSpeedKhz(), std::nullopt);
}

TEST_F(ConnectionTest, StoredFixedSpeedStaysFixed) {
    // The old explicit 2000 kHz default is not migrated (ADR-0111).
    ASSERT_TRUE(app().settingsStore()->setValue(QStringLiteral("jlink/speedKhz"), 2000));
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 683012345));
    EXPECT_FALSE(connection().autoSpeed());
    EXPECT_EQ(connection().speedKhz(), 2000);
    EXPECT_EQ(connection().options().sessionSpeedKhz(), 2000);
}

TEST_F(ConnectionTest, OutOfRangeStoredSpeedIsAutomatic) {
    // Written by hand into the file (the store rejects it through setValue).
    editSettingsFile([](QSettings& raw) {
        raw.setValue(QStringLiteral("probes/683012345/speedKhz"), 99999);
        raw.setValue(QStringLiteral("probes/recent"), QStringLiteral("683012345"));
    });
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 683012345));
    EXPECT_TRUE(connection().autoSpeed());
    EXPECT_EQ(connection().speedKhz(), 4000);
}

TEST_F(ConnectionTest, PerProbeSpeedWinsOverTheGlobalOne) {
    ASSERT_TRUE(app().settingsStore()->setValue(QStringLiteral("jlink/speedKhz"), 1000));
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 683012345));
    connection().setAutoSpeed(true); // remembered as 0 for this probe
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 683012346));
    connection().setSpeedKhz(8000);

    app().newTab();
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 683012345));
    EXPECT_TRUE(connection().autoSpeed());
    EXPECT_EQ(connection().options().sessionSpeedKhz(), std::nullopt);
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 683012346));
    EXPECT_EQ(connection().options().sessionSpeedKhz(), 8000);
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 1));
    EXPECT_EQ(connection().options().sessionSpeedKhz(), 1000); // the global default
}

TEST_F(ConnectionTest, SlowSpeedWarningBelow125Khz) {
    // ADR-0110: only for a fixed speed below 125 kHz.
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 683012345));
    connection().setSpeedKhz(100);
    EXPECT_TRUE(connection().speedWarning().isEmpty()); // automatic
    connection().setAutoSpeed(false);
    const std::optional<std::string> expected = hio::device::slowSpeedWarning(100);
    ASSERT_TRUE(expected.has_value());
    EXPECT_EQ(connection().speedWarning(),
              QString::fromStdString(expected.value_or(std::string())));
    EXPECT_TRUE(connection().speedWarning().contains(QStringLiteral("1000 kHz")));
    connection().setSpeedKhz(125);
    EXPECT_TRUE(connection().speedWarning().isEmpty());
    connection().setSpeedKhz(124);
    EXPECT_FALSE(connection().speedWarning().isEmpty());
    connection().setSpeedKhz(4000);
    EXPECT_TRUE(connection().speedWarning().isEmpty());
}

TEST_F(ConnectionTest, OptionsAreRememberedPerProbe) {
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 683012345));
    connection().setAutoSpeed(false);
    connection().setSpeedKhz(4100);
    connection().setResetOnOpen(true);
    connection().setSpeedKhz(1); // out of range: ignored
    connection().setLatencyMs(5000);
    EXPECT_EQ(connection().speedKhz(), 4100);
    EXPECT_EQ(connection().latencyMs(), 50);
    EXPECT_EQ(app().settingsStore()->value(QStringLiteral("probes/683012345/speedKhz")).toInt(),
              4100);
    EXPECT_EQ(app().settingsStore()->value(QStringLiteral("probes/recent")).toString(),
              QStringLiteral("683012345"));

    // Another tab with the same probe gets the remembered values; another probe the defaults.
    app().newTab();
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 683012345));
    EXPECT_FALSE(connection().autoSpeed());
    EXPECT_EQ(connection().speedKhz(), 4100);
    EXPECT_TRUE(connection().resetOnOpen());
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 1));
    EXPECT_TRUE(connection().autoSpeed());
    EXPECT_FALSE(connection().resetOnOpen());
}

TEST_F(ConnectionTest, AtMost32ProbesAreRemembered) {
    for (int serial = 1; serial <= 40; ++serial) {
        ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"),
                                    static_cast<std::uint32_t>(serial)));
        connection().setAutoSpeed(false);
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
    // The groups of the forgotten probes are removed from the file (ADR-0092).
    const QSettings raw(QString::fromStdU16String(settingsFile().u16string()),
                        QSettings::IniFormat);
    EXPECT_FALSE(raw.contains(QStringLiteral("probes/3/speedKhz")));
    EXPECT_FALSE(raw.childGroups().isEmpty());
    EXPECT_TRUE(raw.contains(QStringLiteral("probes/39/speedKhz")));
    // A forgotten probe gets the defaults again.
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 3));
    EXPECT_TRUE(connection().autoSpeed());
    ASSERT_TRUE(tab().configure(QStringLiteral("chester"), QStringLiteral("app"), 39));
    EXPECT_EQ(connection().speedKhz(), 1039);
}

} // namespace
