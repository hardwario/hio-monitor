// BleSession end to end on the scenario-driven fake backend (plan 0011 B4, ADR-0087, ADR-0127):
// a shell command answered with the prompt, and a link drop recovered by auto-reconnect.

#include "fake/fake_ble_api.h"
#include "hio/core/error.h"
#include "hio/device/ble_session.h"
#include "hio/device/console_line.h"
#include "hio/device/console_session.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/device/session_registry.h"
#include "hio/hw/ble/ble_api.h"
#include "hio/hw/ble/ble_host.h"
#include "session_test_support.h"

#include <QList>
#include <QObject>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hio::device::BleSession;
using hio::device::ConnectionState;
using hio::device::ConsoleLine;
using hio::device::test::waitFor;
using namespace std::chrono_literals;

hio::ble::BleHost::ApiFactory fake(const char* name) {
    return [file = fs::path(HIO_BLE_SCENARIOS_DIR) / name]() -> std::unique_ptr<hio::ble::IBleApi> {
        auto scenario = hio::ble::loadFakeScenario(file);
        return scenario ? std::make_unique<hio::ble::FakeBleApi>(std::move(*scenario)) : nullptr;
    };
}

using BleSessionOptionsFactory = std::function<hio::device::BleSessionOptions()>;

class BleSessionFake : public ::testing::Test {
protected:
    [[nodiscard]] hio::device::RegistryOptions registryOptions() const {
        const fs::path root(m_dir.path().toStdString());
        return {.instanceRoot = root / "instances",
                .lockDirectory = root / "locks",
                .consoleDirectory = root / "console"};
    }

private:
    QTemporaryDir m_dir;
};

TEST_F(BleSessionFake, CommandIsAnsweredBeforeThePrompt) {
    hio::device::SessionRegistry registry(registryOptions());
    hio::ble::BleHost host(fake("chester.json"));
    auto session = BleSession::open(registry, host,
                                    {.profile = hio::device::ProfileRegistry::find("chester"),
                                     .deviceId = "F1:22:33:44:55:66",
                                     .deviceName = "CHESTER-ABC123",
                                     .autoReconnect = false,
                                     .consoleFile = fs::path(),
                                     .consoleLog = nullptr});
    ASSERT_TRUE(session) << session.error().message;
    std::vector<std::string> lines;
    int prompts = 0;
    QObject::connect(session->get(), &BleSession::linesReceived, session->get(),
                     [&lines](const QList<ConsoleLine>& received) {
                         for (const ConsoleLine& line : received) {
                             lines.push_back(line.text);
                         }
                     });
    QObject::connect(session->get(), &BleSession::promptSeen, session->get(),
                     [&prompts] { ++prompts; });
    ASSERT_TRUE(hio::device::waitUntilConnected(**session, 5s));
    ASSERT_TRUE(waitFor([&prompts] { return prompts == 1; })); // after connecting
    ASSERT_TRUE((*session)->sendLine("version"));
    ASSERT_TRUE(waitFor([&prompts] { return prompts == 2; }));
    EXPECT_EQ(lines, (std::vector<std::string>{"version", "Firmware version: v3.1.0"}));
    EXPECT_EQ((*session)->log().lastSeq(), 2U);
}

// close() returns at once; the same device opens again right away (plan 0011 decisions).
TEST_F(BleSessionFake, CloseAndReopenAtOnce) {
    hio::device::SessionRegistry registry(registryOptions());
    hio::ble::BleHost host(fake("chester.json"));
    const BleSessionOptionsFactory make = [] {
        return hio::device::BleSessionOptions{.profile =
                                                  hio::device::ProfileRegistry::find("chester"),
                                              .deviceId = "F1:22:33:44:55:66",
                                              .deviceName = "CHESTER-ABC123",
                                              .autoReconnect = false,
                                              .consoleFile = std::nullopt,
                                              .consoleLog = nullptr};
    };
    for (int round = 0; round < 3; ++round) {
        auto session = BleSession::open(registry, host, make());
        ASSERT_TRUE(session) << session.error().message;
        ASSERT_TRUE(hio::device::waitUntilConnected(**session, 5s)) << round;
        int prompts = 0;
        QObject::connect(session->get(), &BleSession::promptSeen, session->get(),
                         [&prompts] { ++prompts; });
        ASSERT_TRUE((*session)->sendLine("version"));
        ASSERT_TRUE(waitFor([&prompts] { return prompts >= 1; }));
        session->reset(); // close() without waiting, then destruction
    }
    host.stop();
}

// Review B2: the adapter turns off while the session is Disconnected with auto-reconnect: the
// background scan fails with ble.adapter_off and the session stops (Q9) instead of spinning.
TEST_F(BleSessionFake, AdapterOffWhileReconnectingStops) {
    hio::device::SessionRegistry registry(registryOptions());
    hio::ble::BleHost host(fake("adapter_off_after_drop.json"));
    auto session = BleSession::open(registry, host,
                                    {.profile = hio::device::ProfileRegistry::find("chester"),
                                     .deviceId = "F1:22:33:44:55:66",
                                     .deviceName = "",
                                     .autoReconnect = true,
                                     .consoleFile = fs::path(),
                                     .consoleLog = nullptr});
    ASSERT_TRUE(session) << session.error().message;
    std::vector<hio::device::ConnectionEvent> events;
    QObject::connect(
        session->get(), &BleSession::connectionChanged, session->get(),
        [&events](const hio::device::ConnectionEvent& event) { events.push_back(event); });
    ASSERT_TRUE(waitFor(
        [&events] { return !events.empty() && events.back().state == ConnectionState::Stopped; },
        15s));
    ASSERT_GE(events.size(), 3U);
    EXPECT_EQ(events.at(events.size() - 2).state, ConnectionState::Disconnected);
    EXPECT_EQ(events.back().error.value_or(hio::Error{}).code, hio::ErrorCode::BleAdapterOff);
}

TEST_F(BleSessionFake, DropIsRecoveredWithAutoReconnect) {
    hio::device::SessionRegistry registry(registryOptions());
    hio::ble::BleHost host(fake("drop.json"));
    auto session = BleSession::open(registry, host,
                                    {.profile = hio::device::ProfileRegistry::find("chester"),
                                     .deviceId = "F1:22:33:44:55:66",
                                     .deviceName = "",
                                     .autoReconnect = true,
                                     .consoleFile = fs::path(),
                                     .consoleLog = nullptr});
    ASSERT_TRUE(session) << session.error().message;
    std::vector<ConnectionState> states;
    QObject::connect(
        session->get(), &BleSession::connectionChanged, session->get(),
        [&states](const hio::device::ConnectionEvent& event) { states.push_back(event.state); });
    ASSERT_TRUE(waitFor(
        [&states] { return states.size() >= 4 && states.back() == ConnectionState::Connected; },
        15s));
    // An automatic attempt does not report Connecting (as DeviceSession).
    EXPECT_EQ(states, (std::vector<ConnectionState>{
                          ConnectionState::Connecting, ConnectionState::Connected,
                          ConnectionState::Disconnected, ConnectionState::Connected}));
    EXPECT_TRUE((*session)->rssi().has_value());
}

} // namespace

namespace {

// ADR-0156 (M6): the default console file of a BLE session goes into consoleFileDir.
TEST_F(BleSessionFake, ConsoleFileDir) {
    hio::device::SessionRegistry registry(registryOptions());
    hio::ble::BleHost host(fake("chester.json"));
    const fs::path dir = registryOptions().instanceRoot.parent_path() / "mcp-console";
    fs::create_directories(dir);
    auto session = BleSession::open(registry, host,
                                    {.profile = hio::device::ProfileRegistry::find("chester"),
                                     .deviceId = "F1:22:33:44:55:66",
                                     .deviceName = "CHESTER-ABC123",
                                     .autoReconnect = false,
                                     .consoleFile = std::nullopt,
                                     .consoleLog = nullptr,
                                     .consoleFileDir = dir});
    ASSERT_TRUE(session) << session.error().message;
    ASSERT_TRUE(hio::device::waitUntilConnected(**session, 5s));
    ASSERT_TRUE(waitFor([&session] { return !(*session)->consoleFile().empty(); }));
    EXPECT_EQ((*session)->consoleFile().parent_path(), dir);
    EXPECT_TRUE((*session)->consoleFile().filename().string().starts_with("chester-ble-"));
}

} // namespace
