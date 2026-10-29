// FakeBleApi (ADR-0087, ADR-0127) through BleHost and BleScanner with the shared scenarios
// (tests/ble_scenarios): scenario loading, scan rows (de-duplication by id, the 256-device bound,
// RSSI order, --all), adapter states, a scripted shell session, pairing, thread checks.

#include "fake/fake_ble_api.h"
#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/hw/ble/ble_api.h"
#include "hwio/hw/ble/ble_host.h"
#include "hwio/hw/ble/ble_scanner.h"
#include "hwio/hw/ble/link_handler.h"
#include "hwio/hw/ble/types.h"
#include "mock_ble_api.h"

#include <QList>
#include <QObject>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using hwio::ErrorCode;
using hwio::ble::BleHost;
using hwio::ble::BleScanner;
using hwio::ble::FakeBleApi;
using hwio::ble::ScanOptions;
using hwio::ble::test::runUntil;
using namespace std::chrono_literals;

std::filesystem::path scenario(const char* name) {
    return std::filesystem::path(HWIO_BLE_SCENARIOS_DIR) / name;
}

BleHost::ApiFactory fakeFactory(const char* name) {
    return [file = scenario(name)]() -> std::unique_ptr<hwio::ble::IBleApi> {
        auto loaded = hwio::ble::loadFakeScenario(file);
        if (!loaded) {
            return nullptr;
        }
        return std::make_unique<FakeBleApi>(std::move(*loaded));
    };
}

hwio::ble::ProfileMatcher chesterMatcher() {
    return [](const hwio::ble::Advertisement& advertisement) {
        return advertisement.name.find("CHESTER") != std::string::npos ? std::string("chester")
                                                                       : std::string();
    };
}

struct ScanResult {
    std::vector<hwio::ble::BleDeviceInfo> devices;
    std::optional<hwio::Error> error;
    bool finished = false;
    int signalCount = 0;
};

ScanResult scan(const char* name, bool all, std::chrono::milliseconds duration = 300ms) {
    BleHost host(fakeFactory(name));
    BleScanner scanner(host);
    ScanResult result;
    QObject::connect(&scanner, &BleScanner::devicesChanged, &scanner,
                     [&result](const QList<hwio::ble::BleDeviceInfo>&) { ++result.signalCount; });
    QObject::connect(&scanner, &BleScanner::finished, &scanner,
                     [&result](const std::optional<hwio::Error>& error) {
                         result.finished = true;
                         result.error = error;
                     });
    EXPECT_TRUE(
        scanner.start(ScanOptions{.duration = duration, .matcher = chesterMatcher(), .all = all}));
    EXPECT_TRUE(runUntil([&result] { return result.finished; }));
    result.devices = scanner.devices();
    return result;
}

TEST(FakeScenario, LoadsDevicesAndGenerated) {
    const auto chester = hwio::ble::loadFakeScenario(scenario("chester.json"));
    ASSERT_TRUE(chester.has_value());
    ASSERT_EQ(chester->devices.size(), 4U);
    EXPECT_EQ(chester->devices[0].responses.at("version"), "Firmware version: v3.1.0\n");
    const auto many = hwio::ble::loadFakeScenario(scenario("many.json"));
    ASSERT_TRUE(many.has_value());
    EXPECT_EQ(many->devices.size(), 300U);
    EXPECT_FALSE(hwio::ble::loadFakeScenario(scenario("missing.json")).has_value());
}

TEST(BleScanner, ProfileMatchesSortedByRssi) {
    const ScanResult result = scan("chester.json", false);
    EXPECT_FALSE(result.error.has_value());
    ASSERT_EQ(result.devices.size(), 2U);
    EXPECT_EQ(result.devices[0].name, "CHESTER-ABC123");
    EXPECT_EQ(result.devices[0].rssi, -58);
    EXPECT_EQ(result.devices[0].profileId, "chester");
    EXPECT_TRUE(result.devices[0].nusAdvertised);
    EXPECT_EQ(result.devices[1].id, "C0:11:22:33:44:55");
    EXPECT_GE(result.signalCount, 1);
}

TEST(BleScanner, AllKeepsDevicesWithoutAProfile) {
    const ScanResult result = scan("chester.json", true);
    ASSERT_EQ(result.devices.size(), 4U);
    EXPECT_EQ(result.devices[0].name, "Phone"); // the strongest signal first
    EXPECT_TRUE(result.devices[0].profileId.empty());
    EXPECT_FALSE(result.devices[0].nusAdvertised);
}

TEST(BleScanner, DevicesWithEqualNamesAreKeptApart) {
    const ScanResult result = scan("same_names.json", false);
    ASSERT_EQ(result.devices.size(), 2U); // legacy bug: de-duplicated by name
    EXPECT_NE(result.devices[0].id, result.devices[1].id);
}

// ADR-0183 (review L-19): an advertised name cannot forge lines or drive a terminal.
TEST(BleScanner, AdvertisedNamesAreSanitised) {
    const ScanResult result = scan("hostile_name.json", false);
    ASSERT_EQ(result.devices.size(), 1U);
    EXPECT_EQ(result.devices[0].name, "CHESTER-EVILfake line\xEF\xBF\xBD"
                                      "2J");
}

TEST(BleScanner, ListIsBounded) {
    const ScanResult result = scan("many.json", false);
    EXPECT_EQ(result.devices.size(), BleScanner::maxDevices);
    for (std::size_t i = 1; i < result.devices.size(); ++i) {
        EXPECT_GE(result.devices[i - 1].rssi, result.devices[i].rssi);
    }
}

TEST(BleScanner, AdapterProblemsEndTheScan) {
    for (const auto& [file, code] :
         {std::pair{"adapter_off.json", ErrorCode::BleAdapterOff},
          std::pair{"no_adapter.json", ErrorCode::BleAdapterNotFound},
          std::pair{"permission_denied.json", ErrorCode::BlePermissionDenied},
          std::pair{"stack_unavailable.json", ErrorCode::BleStackUnavailable}}) {
        const ScanResult result = scan(file, false);
        ASSERT_TRUE(result.error.has_value()) << file;
        EXPECT_EQ(result.error.value_or(hwio::Error{}).code, code) << file;
        EXPECT_TRUE(result.devices.empty());
    }
}

TEST(BleScanner, DiscoveryErrorWithoutAnyLink) {
    // Legacy bug: the discovery error handler dereferenced a controller that did not exist.
    const ScanResult result = scan("scan_error.json", false);
    ASSERT_TRUE(result.error.has_value());
    EXPECT_EQ(result.error.value_or(hwio::Error{}).code, ErrorCode::BleStackUnavailable);
}

TEST(BleScanner, NamesAreCutAtACharacterBoundary) {
    EXPECT_EQ(hwio::ble::truncateUtf8("CHESTER", 64), "CHESTER");
    // "č" is two bytes: cutting inside it drops it.
    EXPECT_EQ(hwio::ble::truncateUtf8(std::string("ab\xc4\x8d"), 3), "ab");
    EXPECT_EQ(hwio::ble::truncateUtf8(std::string(100, 'x'), 64).size(), 64U);
}

// A thread-safe text log of a link handler.
struct SharedLog {
    std::mutex mutex;
    std::vector<std::string> events;
    std::string data;
    bool closed = false;
};

class LogHandler : public hwio::ble::ILinkHandler {
public:
    explicit LogHandler(std::shared_ptr<SharedLog> log) : m_log(std::move(log)) {}
    void onConnecting(bool /*automatic*/) override { add("connecting"); }
    void onPairingPrompt() override { add("prompt"); }
    void onReady(hwio::ble::LinkReady ready) override { add("ready " + std::to_string(ready.mtu)); }
    void onData(std::span<const std::byte> data) override {
        const std::scoped_lock lock(m_log->mutex);
        for (const std::byte b : data) {
            m_log->data.push_back(static_cast<char>(b));
        }
    }
    void onLineWritten(std::string line) override { add("written " + line); }
    void onLineRejected(std::string line, hwio::Error /*error*/) override {
        add("rejected " + line);
    }
    void onDown(hwio::Error error) override { add("down " + std::string(hwio::idOf(error.code))); }
    void onPairingFinished(hwio::Status status, bool alreadyPaired) override {
        add(std::string("paired ") + (status ? "ok" : "fail") + (alreadyPaired ? " already" : ""));
    }
    void onFlush() override {}
    void onClosed() override {
        const std::scoped_lock lock(m_log->mutex);
        m_log->closed = true;
    }

private:
    void add(std::string event) {
        const std::scoped_lock lock(m_log->mutex);
        m_log->events.push_back(std::move(event));
    }
    std::shared_ptr<SharedLog> m_log;
};

bool hasEvent(SharedLog& log, const std::string& event) {
    const std::scoped_lock lock(log.mutex);
    return std::ranges::find(log.events, event) != log.events.end();
}

TEST(FakeBleApi, ShellAnswersWithThePrompt) {
    const int violations = hwio::ble::fakeBleThreadViolations();
    auto log = std::make_shared<SharedLog>();
    {
        BleHost host(fakeFactory("chester.json"));
        const auto client = host.newClientId();
        ASSERT_TRUE(host.post(hwio::ble::OpenLink{.client = client,
                                                  .device = "F1:22:33:44:55:66",
                                                  .uuids = {},
                                                  .handler = std::make_unique<LogHandler>(log),
                                                  .timing = {}}));
        ASSERT_TRUE(runUntil([&] { return hasEvent(*log, "ready 247"); }));
        ASSERT_TRUE(host.post(hwio::ble::SendLine{.client = client, .line = "version"}));
        ASSERT_TRUE(runUntil([&] {
            const std::scoped_lock lock(log->mutex);
            // chester.json is CHESTER: no prompt on connect, a new line first (HIL H5).
            return log->data == "\r\nFirmware version: v3.1.0\nbt_nus:~$ ";
        }));
        EXPECT_TRUE(hasEvent(*log, "written version"));
    }
    const std::scoped_lock lock(log->mutex);
    EXPECT_TRUE(log->closed);
    EXPECT_EQ(hwio::ble::fakeBleThreadViolations(), violations);
}

TEST(FakeBleApi, ConnectErrorAndUnknownDevice) {
    auto paired = std::make_shared<SharedLog>();
    auto unknown = std::make_shared<SharedLog>();
    BleHost host(fakeFactory("not_paired.json"));
    ASSERT_TRUE(host.post(hwio::ble::OpenLink{.client = 1,
                                              .device = "F1:22:33:44:55:66",
                                              .uuids = {},
                                              .handler = std::make_unique<LogHandler>(paired),
                                              .timing = {}}));
    ASSERT_TRUE(host.post(hwio::ble::OpenLink{.client = 2,
                                              .device = "00:00:00:00:00:00",
                                              .uuids = {},
                                              .handler = std::make_unique<LogHandler>(unknown),
                                              // H6: retried until this deadline
                                              .timing = {.connectDeadline = 300ms,
                                                         .connectRetryDelay = 50ms,
                                                         .connectBusyRetryDelay = 50ms}}));
    EXPECT_TRUE(runUntil([&] { return hasEvent(*paired, "down ble.not_paired"); }));
    EXPECT_TRUE(runUntil([&] { return hasEvent(*unknown, "down ble.connect_failed"); }));
}

TEST(FakeBleApi, PairingOutcomes) {
    BleHost host(fakeFactory("unpaired.json"));
    auto subscriber = BleHost::subscribe([] {});
    for (const char* device : {"F1:22:33:44:55:66", "C0:11:22:33:44:55"}) {
        ASSERT_TRUE(host.post(hwio::ble::PairDevice{
            .device = device, .subscriber = subscriber, .client = std::nullopt}));
    }
    ASSERT_TRUE(runUntil([&] { return subscriber->size() >= 2; }));
    std::vector<hwio::ble::PairingResult> results;
    while (auto event = subscriber->pop()) {
        results.push_back(std::get<hwio::ble::PairingResult>(*event));
    }
    ASSERT_EQ(results.size(), 2U);
    for (const auto& result : results) {
        if (result.device == "F1:22:33:44:55:66") {
            EXPECT_TRUE(result.status.has_value());
            EXPECT_FALSE(result.alreadyPaired);
        } else {
            ASSERT_FALSE(result.status.has_value());
            EXPECT_EQ(result.status.error().code, ErrorCode::BlePairingFailed);
        }
    }
    // Now bonded: "Already paired."
    ASSERT_TRUE(host.post(hwio::ble::PairDevice{
        .device = "F1:22:33:44:55:66", .subscriber = subscriber, .client = std::nullopt}));
    ASSERT_TRUE(runUntil([&] { return subscriber->size() >= 1; }));
    EXPECT_TRUE(
        std::get<hwio::ble::PairingResult>(subscriber->pop().value_or(hwio::ble::BleEvent{}))
            .alreadyPaired);
    subscriber->detach();
}

TEST(FakeBleApi, MacOsDoesNotPairOnRequest) {
    BleHost host(fakeFactory("macos.json"));
    auto subscriber = BleHost::subscribe([] {});
    ASSERT_TRUE(host.post(hwio::ble::PairDevice{.device = "6A0B3F1C-1D2E-4F50-8A9B-0C1D2E3F4A5B",
                                                .subscriber = subscriber,
                                                .client = std::nullopt}));
    ASSERT_TRUE(runUntil([&] { return subscriber->size() >= 1; }));
    const auto result =
        std::get<hwio::ble::PairingResult>(subscriber->pop().value_or(hwio::ble::BleEvent{}));
    ASSERT_FALSE(result.status.has_value());
    EXPECT_EQ(result.status.error().code, ErrorCode::BlePairingNotSupported);
    subscriber->detach();
}

TEST(FakeBleApi, PermissionUndeterminedIsNotAnError) {
    BleHost host(fakeFactory("permission_undetermined.json"));
    const auto adapter = host.queryAdapter(2s);
    ASSERT_TRUE(adapter.has_value());
    EXPECT_EQ(adapter->state, hwio::ble::AdapterState::PermissionUndetermined);
    EXPECT_FALSE(hwio::ble::adapterError(*adapter).has_value());
}

} // namespace
