#pragma once

// FakeBleApi - a scenario-driven IBleApi for tests (ADR-0087, ADR-0127). Compiled only with
// HIO_TEST_OVERRIDES; createBleApi() returns it when HIO_FAKE_BLE_SCENARIO names a scenario file
// (tests/ble_scenarios/*.json). Runs on the BLE thread with QTimers.
//
// Scenario (JSON object, every field optional):
//   "adapter": {"state": "ready" | "notFound" | "poweredOff" | "permissionDenied" |
//               "permissionUndetermined" | "stackUnavailable", "name": "hci0",
//               "address": "00:11:22:33:44:55"}
//   "adapterLater": {"state": ...}    the adapter from the second adapter() call on (e.g. after a
//                                     permission request)
//   "pairingSupported": true          false = macOS: requestPairing -> ble.pairing_not_supported
//   "scanError": "<error id>"         startScan fails with it
//   "advertiseIntervalMs": 1000       every advertising device is reported this often
//   "generate": {"count": 300, "namePrefix": "CHESTER-GEN", "rssi": -80}   extra devices
//   "devices": [{
//     "id": "F1:22:33:44:55:66", "name": "CHESTER-ABC123", "rssi": -58,
//     "nus": true,                    advertises the NUS service UUID
//     "advertise": true,              seen by scans
//     "paired": true,                 IBleApi::isPaired
//     "pairing": "ok" | "fail" | "noAgent" | "never",   result of requestPairing (after 100 ms;
//                                     noAgent = ble.no_pairing_agent, HIL H1)
//     "connectDelayMs": 20,           until the CCCD write starts
//     "connectError": "<error id>",   the connect attempt fails with it (after the delay)
//     "connectErrorOnce": false,      only the first attempt fails
//     "pairingDelayMs": 0,            > 0: the CCCD write stays pending this long (passkey)
//     "mtu": 247, "writeWithoutResponse": false,
//     "writeStall": false,            writes are never confirmed
//     "writeError": "<error id>",     every write fails with it
//     "echo": false,                  echo written lines
//     "newlineAfterCommand": false,   "\r\n" after each received line, in its own notification
//                                     before the response (a shell without echo, CHESTER, H5)
//     "prompt": "bt_nus:~$ ",         sent after the greeting and after every line's response
//     "promptOnConnect": true,        false: no prompt when the link is ready (CHESTER, H5)
//     "greeting": "",                 sent when the link is ready
//     "responses": {"version": "Firmware version: v3.1.0\n"},
//     "output": [{"atMs": 100, "text": "..."}],   sent after ready
//     "dropAfterMs": 0                > 0: the link is lost (ble.disconnected) that long after the
//                                     first ready
//     "adapterOffAfterDrop": false    the adapter turns off with that drop (poweredOff)
//   }]
//
// Test hook: fakeBleThreadViolations() counts calls (and destruction) on another thread than the
// one that created the fake (plan 0011: QtBluetooth objects are destroyed on the BLE thread).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/hw/ble/ble_api.h"
#include "hio/hw/ble/types.h"

#include <QObject>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

class QThread;

namespace hio::ble {

struct FakeOutput {
    int atMs = 0;
    std::string text;
};

struct FakeDevice {
    BleDeviceId id;
    std::string name;
    int rssi = -60;
    bool nus = true;
    bool advertise = true;
    bool paired = true;
    std::string pairing = "ok";
    int connectDelayMs = 20;
    std::optional<ErrorCode> connectError;
    bool connectErrorOnce = false;
    int pairingDelayMs = 0;
    std::uint16_t mtu = 247;
    bool writeWithoutResponse = false;
    bool writeStall = false;
    std::optional<ErrorCode> writeError;
    bool echo = false;
    bool newlineAfterCommand = false;
    std::string prompt;
    bool promptOnConnect = true;
    std::string greeting;
    std::map<std::string, std::string> responses;
    std::vector<FakeOutput> output;
    int dropAfterMs = 0;
    bool adapterOffAfterDrop = false;
};

struct FakeScenario {
    AdapterInfo adapter{.state = AdapterState::Ready, .name = "fake0", .address = ""};
    std::optional<AdapterInfo> adapterLater;
    bool pairingSupported = true;
    std::optional<ErrorCode> scanError;
    int advertiseIntervalMs = 1000;
    std::vector<FakeDevice> devices;
};

// Errors: io.file_not_found, io.read_failed (not a JSON object).
[[nodiscard]] Result<FakeScenario> loadFakeScenario(const std::filesystem::path& file);

class FakeBleApi final : public IBleApi {
public:
    explicit FakeBleApi(FakeScenario scenario);
    ~FakeBleApi() override;
    FakeBleApi(const FakeBleApi&) = delete;
    FakeBleApi& operator=(const FakeBleApi&) = delete;
    FakeBleApi(FakeBleApi&&) = delete;
    FakeBleApi& operator=(FakeBleApi&&) = delete;

    void setListener(IBleListener* listener) override;
    [[nodiscard]] AdapterInfo adapter() override;
    [[nodiscard]] Status startScan(std::chrono::milliseconds duration) override;
    void stopScan() override;
    [[nodiscard]] Result<LinkId> connect(const BleDeviceId& device, const NusUuids& uuids) override;
    [[nodiscard]] Status write(LinkId id, std::span<const std::byte> chunk, WriteMode mode,
                               std::uint64_t token) override;
    void disconnect(LinkId id) override;
    [[nodiscard]] Status requestPairing(const BleDeviceId& device) override;
    [[nodiscard]] bool isPaired(const BleDeviceId& device) override;

private:
    struct Link {
        LinkId id = 0;
        std::size_t device = 0;           // index into m_scenario.devices
        std::unique_ptr<QObject> context; // owns the link's pending timers
        std::string input;                // received bytes of the current line
        bool ready = false;
        // The shell answers lines in order: no answer is sent before an earlier one (H5).
        std::chrono::steady_clock::time_point nextAnswer{};
        // The answers not sent yet, in order (due times never decrease). Each timer only wakes
        // sendDueAnswers(): Windows fired timers with (nearly) the same deadline out of order,
        // which reordered the shell's output (plan 0023). Bounded by the lines in flight.
        struct PendingAnswer {
            std::chrono::steady_clock::time_point due;
            std::string text;
        };
        std::deque<PendingAnswer> answers;
    };

    void checkThread() const;
    void advertise();
    void startLink(LinkId id);
    void linkReady(LinkId id);
    void send(LinkId id, const std::string& text);
    void answer(Link& link, std::chrono::milliseconds delay, std::string text);
    void sendDueAnswers(LinkId id);
    void received(LinkId id, std::string_view bytes);
    [[nodiscard]] Link* find(LinkId id);
    [[nodiscard]] FakeDevice* device(const BleDeviceId& id);

    FakeScenario m_scenario;
    IBleListener* m_listener = nullptr;
    QThread* m_thread;                      // non-owning: the creating thread
    std::unique_ptr<QObject> m_context;     // owns the pairing timers (BLE thread)
    std::unique_ptr<QObject> m_scanContext; // owns the scan timers; reset = stopped
    std::map<LinkId, Link> m_links;         // bounded by the host's links
    std::map<std::size_t, int> m_attempts;  // connect attempts per device
    std::map<std::size_t, bool> m_dropped;  // the scripted drop happened
    LinkId m_nextLink = 1;
    int m_adapterQueries = 0;
};

// Calls on (or destruction from) a thread other than the creating one, in this process.
[[nodiscard]] int fakeBleThreadViolations() noexcept;

} // namespace hio::ble
