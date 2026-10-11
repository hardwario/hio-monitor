#pragma once

// An in-process, thread-safe IJLinkApi whose probe, target and RTT state the test controls while a
// session worker uses it (deterministic DeviceSession tests; the fake library covers the real
// loader in the integration tests).

#include "hio/core/result.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/types.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace hio::device::test {

using jlink::RttBufferInfo;
using jlink::RttDirection;

// The simulated probe + target. Every member is guarded by `mutex` (use set()/get()).
class ScriptedTarget {
public:
    // ---- configuration (test thread) ----
    template <class F> void set(const F& change) {
        const std::scoped_lock lock(mutex);
        change(*this);
    }
    template <class F> auto get(const F& read) {
        const std::scoped_lock lock(mutex);
        return read(*this);
    }

    // Bytes the target "prints" on an up buffer (0 = terminal, 1 = logger).
    void output(int buffer, std::string_view bytes) {
        set([&](ScriptedTarget& t) { t.pending[buffer] += bytes; });
    }
    [[nodiscard]] std::string written() {
        return get([](ScriptedTarget& t) { return t.down; });
    }

    std::vector<jlink::ProbeInfo> probes{
        jlink::ProbeInfo{.serialNumber = 801000001,
                         .product = "Test Probe",
                         .connection = jlink::Connection::Usb,
                         .firmware = {}},
    };
    std::string firmware = "J-Link V1 compiled Oct  1 2024 10:00:00";
    std::string libraryFirmware = "J-Link V1 compiled Oct  1 2024 10:00:00";
    std::optional<std::string> openError;
    int connectFailures = 0; // the next N connect() calls fail
    std::chrono::milliseconds connectDelay{0};
    std::chrono::milliseconds openDelay{0};
    int vtref = 3300;                 // the real target voltage (connect needs >= 1000 mV)
    std::optional<int> reportedVtref; // what the probe reports (default: vtref)
    bool hardwareStatusFails = false;
    bool controlBlockFound = true;
    std::vector<RttBufferInfo> up{{.name = "Terminal", .size = 1024, .flags = 0},
                                  {.name = "Logger", .size = 1024, .flags = 0}};
    std::vector<RttBufferInfo> downBuffers{{.name = "Terminal", .size = 16, .flags = 0}};
    std::map<int, std::string> pending;           // per up buffer
    std::string down;                             // everything written to the down buffer
    std::map<std::string, std::string> responses; // written line -> terminal output
    bool writeStall = false;
    bool readFails = false;
    std::optional<std::uint32_t> openedSerial;

    // ---- counters ----
    int opens = 0;
    int opensStarted = 0;    // open() entered (before its delay)
    int connectsStarted = 0; // connect() entered (before its delay)
    int lists = 0;           // listProbes() calls
    int ipLists = 0;         // listProbes(UsbAndIp) calls (network scans)
    int hwStatusCalls = 0;   // hardwareStatus() calls
    int rttReads = 0;        // rttRead() calls
    int connects = 0;
    int resets = 0;
    int rttStarts = 0;
    int rttStops = 0;
    // When each rttStart() / rttStop() happened (retry pacing, review R31; a test runs seconds).
    std::vector<std::chrono::steady_clock::time_point> rttStartTimes;
    std::vector<std::chrono::steady_clock::time_point> rttStopTimes;
    bool open = false;
    bool rttStarted = false;

    // Guards every member (all public, like a struct: the test reads and changes them through
    // set() / get()).
    std::mutex mutex;
};

class ScriptedApi final : public jlink::IJLinkApi {
public:
    explicit ScriptedApi(std::shared_ptr<ScriptedTarget> target) : m_target(std::move(target)) {}

    std::uint32_t libraryVersion() override { return 81000; }
    std::string compileDateTime() override { return "Oct  1 2026 12:00:00"; }

    std::expected<std::vector<jlink::ProbeInfo>, int> listProbes(jlink::ProbeHosts hosts) override {
        return m_target->get([hosts](ScriptedTarget& t) {
            ++t.lists;
            if (hosts == jlink::ProbeHosts::UsbAndIp) {
                ++t.ipLists;
            }
            std::vector<jlink::ProbeInfo> listed;
            for (const jlink::ProbeInfo& probe : t.probes) {
                if (hosts == jlink::ProbeHosts::UsbAndIp ||
                    probe.connection == jlink::Connection::Usb) {
                    listed.push_back(probe);
                }
            }
            return listed;
        });
    }

    int selectBySerialNumber(std::uint32_t serialNumber) override {
        return m_target->get([&](ScriptedTarget& t) {
            const bool known = std::ranges::any_of(t.probes, [&](const jlink::ProbeInfo& p) {
                return p.serialNumber == serialNumber;
            });
            t.openedSerial = serialNumber;
            return known ? 0 : -1;
        });
    }

    std::optional<std::string> open() override {
        std::this_thread::sleep_for(m_target->get([](ScriptedTarget& t) {
            ++t.opensStarted;
            return t.openDelay;
        }));
        return m_target->get([](ScriptedTarget& t) -> std::optional<std::string> {
            ++t.opens;
            if (t.openError) {
                return t.openError;
            }
            t.open = true;
            return std::nullopt;
        });
    }

    void close() override {
        m_target->set([](ScriptedTarget& t) {
            t.open = false;
            t.rttStarted = false;
        });
    }

    bool isOpen() override {
        return m_target->get([](ScriptedTarget& t) { return t.open; });
    }

    std::string execCommand(std::string_view /*command*/) override { return {}; }
    int selectInterface(jlink::TargetInterface /*targetInterface*/) override { return 0; }
    void setSpeed(std::uint32_t /*kiloHertz*/) override {}

    int connect() override {
        std::this_thread::sleep_for(m_target->get([](ScriptedTarget& t) {
            ++t.connectsStarted;
            return t.connectDelay;
        }));
        return m_target->get([](ScriptedTarget& t) {
            ++t.connects;
            if (t.connectFailures > 0) {
                --t.connectFailures;
                return -1;
            }
            return t.vtref >= 1000 ? 0 : -1;
        });
    }

    bool isConnected() override { return true; }

    std::optional<jlink::HardwareStatus> hardwareStatus() override {
        return m_target->get([](ScriptedTarget& t) -> std::optional<jlink::HardwareStatus> {
            ++t.hwStatusCalls;
            if (t.hardwareStatusFails) {
                return std::nullopt;
            }
            return jlink::HardwareStatus{.targetMillivolts = t.reportedVtref.value_or(t.vtref)};
        });
    }

    std::string firmwareString() override {
        return m_target->get([](ScriptedTarget& t) { return t.firmware; });
    }

    std::expected<std::string, int> embeddedFirmwareString(std::string_view /*id*/) override {
        return m_target->get([](ScriptedTarget& t) { return t.libraryFirmware; });
    }

    std::uint32_t updateFirmwareIfNewer() override {
        return m_target->get([](ScriptedTarget& t) -> std::uint32_t {
            if (t.firmware == t.libraryFirmware) {
                return 0;
            }
            t.firmware = t.libraryFirmware;
            return 0x1234;
        });
    }

    std::uint32_t serialNumber() override {
        return m_target->get([](ScriptedTarget& t) { return t.openedSerial.value_or(0); });
    }

    void setResetDelay(int /*milliseconds*/) override {}

    int reset() override {
        return m_target->get([](ScriptedTarget& t) {
            ++t.resets;
            t.rttStarted = false;
            return 0;
        });
    }

    int halt() override { return 0; }
    void go() override {}

    int rttStart() override {
        return m_target->get([](ScriptedTarget& t) {
            ++t.rttStarts;
            t.rttStartTimes.push_back(std::chrono::steady_clock::now());
            t.rttStarted = true;
            return 0;
        });
    }

    int rttStop() override {
        return m_target->get([](ScriptedTarget& t) {
            ++t.rttStops;
            t.rttStopTimes.push_back(std::chrono::steady_clock::now());
            t.rttStarted = false;
            return 0;
        });
    }

    int rttBufferCount(RttDirection direction) override {
        return m_target->get([&](ScriptedTarget& t) {
            if (!t.rttStarted || !t.controlBlockFound) {
                return -2;
            }
            return static_cast<int>(direction == RttDirection::Up ? t.up.size()
                                                                  : t.downBuffers.size());
        });
    }

    std::expected<RttBufferInfo, int> rttBufferInfo(int index, RttDirection direction) override {
        return m_target->get([&](ScriptedTarget& t) -> std::expected<RttBufferInfo, int> {
            const auto& buffers = direction == RttDirection::Up ? t.up : t.downBuffers;
            if (!t.rttStarted || index < 0 || std::cmp_greater_equal(index, buffers.size())) {
                return std::unexpected(-1);
            }
            return buffers.at(static_cast<std::size_t>(index));
        });
    }

    int rttRead(int index, std::span<std::byte> buffer) override {
        return m_target->get([&](ScriptedTarget& t) {
            ++t.rttReads;
            if (!t.rttStarted || t.readFails) {
                return -1;
            }
            std::string& data = t.pending[index];
            const std::size_t count = std::min(buffer.size(), data.size());
            std::ranges::transform(data.substr(0, count), buffer.begin(),
                                   [](char c) { return static_cast<std::byte>(c); });
            data.erase(0, count);
            return static_cast<int>(count);
        });
    }

    int rttWrite(int /*index*/, std::span<const std::byte> data) override {
        return m_target->get([&](ScriptedTarget& t) {
            if (!t.rttStarted) {
                return -1;
            }
            if (t.writeStall) {
                return 0;
            }
            for (const std::byte b : data) {
                const char c = static_cast<char>(b);
                t.down += c;
                if (c == '\n') {
                    const std::size_t start = t.down.rfind('\n', t.down.size() - 2);
                    const std::string line = t.down.substr(
                        start == std::string::npos ? 0 : start + 1,
                        t.down.size() - (start == std::string::npos ? 0 : start + 1) - 1);
                    if (const auto it = t.responses.find(line); it != t.responses.end()) {
                        t.pending[0] += it->second;
                    }
                }
            }
            return static_cast<int>(data.size());
        });
    }

    void flushMessages() override {}
    std::string takeErrorText() override { return {}; }

private:
    std::shared_ptr<ScriptedTarget> m_target;
};

} // namespace hio::device::test
