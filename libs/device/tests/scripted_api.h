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
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace hio::device::test {

using jlink::RttBufferInfo;
using jlink::RttDirection;

// One simulated MCU behind the probe (M2): flash, UICR, FICR part, CTRL-AP protection.
struct ScriptedChip {
    std::uint32_t part = 0x52840;
    bool nrf91 = false;
    bool protectedChip = false;
    std::vector<std::uint8_t> flash = std::vector<std::uint8_t>(0x100000, 0xFF);
    std::vector<std::uint8_t> uicr = std::vector<std::uint8_t>(0x1000, 0xFF);
    // M6: 64 KiB of RAM at 0x20000000 (zero), registers, memory zones, core id.
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(0x10000, 0);
    std::vector<jlink::RegisterValue> registers{{.name = "R0", .value = 1},
                                                {.name = "PC", .value = 0x1000}};
    std::vector<jlink::MemoryZone> zones{
        {.name = "AHB-AP", .description = "AHB-AP (AP0)", .virtualAddress = 0}};
    std::uint32_t coreId = 0x2BA01477U;

    [[nodiscard]] std::uint8_t* ramAt(std::uint64_t address) {
        if (address >= 0x20000000U && address < 0x20000000U + ram.size()) {
            return &ram.at(static_cast<std::size_t>(address - 0x20000000U));
        }
        return nullptr;
    }
    [[nodiscard]] std::uint8_t* readableAt(std::uint64_t address) {
        std::uint8_t* byte = at(address);
        return byte != nullptr ? byte : ramAt(address);
    }

    [[nodiscard]] std::uint32_t uicrBase() const { return nrf91 ? 0x00FF8000U : 0x10001000U; }
    [[nodiscard]] std::uint32_t ficrPart() const { return nrf91 ? 0x00FF0140U : 0x10000100U; }
    [[nodiscard]] std::uint32_t ctrlAp() const { return nrf91 ? 4U : 1U; }
    [[nodiscard]] std::uint8_t* at(std::uint64_t address) {
        if (address < flash.size()) {
            return &flash.at(static_cast<std::size_t>(address));
        }
        if (address >= uicrBase() && address < uicrBase() + uicr.size()) {
            return &uicr.at(static_cast<std::size_t>(address - uicrBase()));
        }
        return nullptr;
    }
    void eraseAll() {
        std::ranges::fill(flash, std::uint8_t{0xFF});
        std::ranges::fill(uicr, std::uint8_t{0xFF});
    }
};

inline ScriptedChip nrf91Chip() {
    ScriptedChip chip;
    chip.part = 0x9120;
    chip.nrf91 = true;
    return chip;
}

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
    std::vector<std::uint32_t> speeds; // every setSpeed() value (0 = automatic, ADR-0090)

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

    // ---- M2: the two MCUs a CHESTER probe can reach (by J-Link device name: NRF91* = lte) ----
    ScriptedChip app;
    ScriptedChip lte = nrf91Chip();
    std::string device;           // the last "Device = ..." command
    std::string failConnectTo;    // connect() with this device name fails
    std::vector<std::string> ops; // connect:<device>, reset, go, halt, download, eraseChip, ...
    bool connected = false;
    bool downloading = false;
    std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> download;
    int endDownloadResult = 0; // < 0: EndDownload fails
    std::chrono::milliseconds endDownloadDelay{0};
    std::chrono::milliseconds readDelay{0}; // per readMemory() call
    std::optional<std::uint32_t> verifyMismatchAt;
    int eraseAllPolls = 1;
    int eraseAllLeft = 0;
    bool eraseAllRunning = false;
    std::uint32_t apSelect = 0;
    int readMemoryCalls = 0;
    int unsecureHooks = 0;          // installUnsecureDialogHook() calls
    bool throwOnReadMemory = false; // readMemory() throws (a dying worker, review R4)
    std::chrono::milliseconds eraseChipDelay{0};
    // ---- M6 ----
    bool halted = false;                          // reset() halts, go() runs, halt() halts
    std::vector<jlink::OptionalFunction> missing; // has() is false for these (ADR-0151)
    std::vector<std::optional<std::uint32_t>> rttStartAddresses; // every rttStart() argument

    [[nodiscard]] ScriptedChip& chip() { return device.starts_with("NRF91") ? lte : app; }

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
            t.connected = false;
            t.rttStarted = false;
            t.ops.emplace_back("close");
        });
    }

    bool isOpen() override {
        return m_target->get([](ScriptedTarget& t) { return t.open; });
    }

    std::string execCommand(std::string_view command) override {
        constexpr std::string_view prefix = "Device = ";
        if (command.starts_with(prefix)) {
            m_target->set(
                [&](ScriptedTarget& t) { t.device = std::string(command.substr(prefix.size())); });
        }
        return {};
    }
    int selectInterface(jlink::TargetInterface /*targetInterface*/) override { return 0; }
    void setSpeed(std::uint32_t kiloHertz) override {
        m_target->set([kiloHertz](ScriptedTarget& t) { t.speeds.push_back(kiloHertz); });
    }

    int connect() override {
        std::this_thread::sleep_for(m_target->get([](ScriptedTarget& t) {
            ++t.connectsStarted;
            return t.connectDelay;
        }));
        return m_target->get([](ScriptedTarget& t) {
            ++t.connects;
            t.ops.push_back("connect:" + t.device);
            t.connected = false;
            if (t.connectFailures > 0) {
                --t.connectFailures;
                return -1;
            }
            if (t.vtref < 1000 || t.chip().protectedChip ||
                (!t.failConnectTo.empty() && t.device == t.failConnectTo)) {
                return -1;
            }
            t.connected = true;
            return 0;
        });
    }

    bool isConnected() override {
        return m_target->get([](ScriptedTarget& t) { return t.connected; });
    }

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
            t.halted = true;
            t.ops.emplace_back("reset");
            return 0;
        });
    }

    int halt() override {
        m_target->set([](ScriptedTarget& t) {
            t.ops.emplace_back("halt");
            t.halted = true;
        });
        return 0;
    }
    void go() override {
        m_target->set([](ScriptedTarget& t) {
            t.ops.emplace_back("go");
            t.halted = false;
        });
    }

    // ---- M6: optional target functions (ADR-0151) ----
    bool has(jlink::OptionalFunction function) override {
        return m_target->get([function](ScriptedTarget& t) {
            return std::ranges::find(t.missing, function) == t.missing.end();
        });
    }
    int isHalted() override {
        return m_target->get([](ScriptedTarget& t) {
            if (!t.connected) {
                return -1;
            }
            return t.halted ? 1 : 0;
        });
    }
    std::uint32_t coreId() override {
        return m_target->get([](ScriptedTarget& t) { return t.chip().coreId; });
    }
    static int writeUnit(ScriptedTarget& t, std::uint32_t address, std::uint32_t value,
                         std::uint32_t size, std::string_view op) {
        t.ops.emplace_back(op);
        for (std::uint32_t b = 0; b < size; ++b) {
            std::uint8_t* byte = t.chip().ramAt(std::uint64_t{address} + b);
            if (!t.connected || byte == nullptr) {
                return -1;
            }
            *byte = static_cast<std::uint8_t>((value >> (8U * b)) & 0xFFU);
        }
        return 0;
    }
    int writeU16(std::uint32_t address, std::uint16_t value) override {
        return m_target->get(
            [&](ScriptedTarget& t) { return writeUnit(t, address, value, 2, "writeU16"); });
    }
    int writeU32(std::uint32_t address, std::uint32_t value) override {
        return m_target->get(
            [&](ScriptedTarget& t) { return writeUnit(t, address, value, 4, "writeU32"); });
    }
    int readU32s(std::uint32_t address, std::span<std::uint32_t> items) override {
        return m_target->get([&](ScriptedTarget& t) {
            if (!t.connected) {
                return -1;
            }
            for (std::size_t i = 0; i < items.size(); ++i) {
                std::uint32_t word = 0;
                for (std::uint32_t b = 0; b < 4; ++b) {
                    const std::uint8_t* byte = t.chip().readableAt(address + (i * 4) + b);
                    if (byte == nullptr) {
                        return -1;
                    }
                    word |= std::uint32_t{*byte} << (8U * b);
                }
                items[i] = word;
            }
            return static_cast<int>(items.size());
        });
    }
    int readMemory16(std::uint32_t address, std::span<std::byte> buffer) override {
        return readMemory(address, buffer);
    }
    std::expected<std::vector<std::uint32_t>, int> registerList() override {
        return m_target->get(
            [](ScriptedTarget& t) -> std::expected<std::vector<std::uint32_t>, int> {
                if (!t.connected) {
                    return std::unexpected(-1);
                }
                std::vector<std::uint32_t> list;
                list.reserve(t.chip().registers.size());
                for (std::size_t i = 0; i < t.chip().registers.size(); ++i) {
                    list.push_back(static_cast<std::uint32_t>(i));
                }
                return list;
            });
    }
    std::string registerName(std::uint32_t index) override {
        return m_target->get([index](ScriptedTarget& t) {
            return index < t.chip().registers.size() ? t.chip().registers[index].name
                                                     : std::string();
        });
    }
    int readRegisters(std::span<const std::uint32_t> indexes,
                      std::span<std::uint32_t> values) override {
        return m_target->get([&](ScriptedTarget& t) {
            for (std::size_t i = 0; i < indexes.size(); ++i) {
                if (indexes[i] >= t.chip().registers.size()) {
                    return -1;
                }
                values[i] = t.chip().registers[indexes[i]].value;
            }
            return 0;
        });
    }
    std::expected<std::vector<jlink::MemoryZone>, int> memoryZones(std::size_t maxZones) override {
        return m_target->get([maxZones](ScriptedTarget& t) {
            std::vector<jlink::MemoryZone> zones = t.chip().zones;
            zones.resize(std::min(zones.size(), maxZones));
            return zones;
        });
    }

    // ---- M2: memory, download, erase, CoreSight ----
    void beginDownload() override {
        m_target->set([](ScriptedTarget& t) {
            t.downloading = true;
            t.download.clear();
        });
    }

    int writeMemory(std::uint32_t address, std::span<const std::byte> data) override {
        return m_target->get([&](ScriptedTarget& t) {
            if (!t.connected) {
                return -1;
            }
            if (!t.downloading) { // M6: a direct write, RAM only
                t.ops.emplace_back("writeMem");
                for (std::size_t i = 0; i < data.size(); ++i) {
                    std::uint8_t* byte = t.chip().ramAt(std::uint64_t{address} + i);
                    if (byte == nullptr) {
                        return -1;
                    }
                    *byte = static_cast<std::uint8_t>(data[i]);
                }
                return static_cast<int>(data.size());
            }
            std::vector<std::uint8_t> bytes;
            for (const std::byte b : data) {
                bytes.push_back(static_cast<std::uint8_t>(b));
            }
            t.download.emplace_back(address, std::move(bytes));
            return static_cast<int>(data.size());
        });
    }

    int endDownload(const jlink::FlashProgressFn& progress) override {
        std::this_thread::sleep_for(
            m_target->get([](ScriptedTarget& t) { return t.endDownloadDelay; }));
        if (progress) {
            progress("Erase", 100);
            progress("Program", 100);
        }
        return m_target->get([](ScriptedTarget& t) {
            t.downloading = false;
            t.ops.emplace_back("download");
            if (t.endDownloadResult < 0 || !t.connected) {
                return t.endDownloadResult < 0 ? t.endDownloadResult : -1;
            }
            for (const auto& [address, bytes] : t.download) {
                for (std::size_t i = 0; i < bytes.size(); ++i) {
                    std::uint8_t* byte = t.chip().at(std::uint64_t{address} + i);
                    if (byte == nullptr) {
                        return -1;
                    }
                    *byte = bytes[i];
                }
            }
            t.download.clear();
            return 0;
        });
    }

    int readMemory(std::uint32_t address, std::span<std::byte> buffer) override {
        std::this_thread::sleep_for(m_target->get([](ScriptedTarget& t) { return t.readDelay; }));
        if (m_target->get([](ScriptedTarget& t) { return t.throwOnReadMemory; })) {
            throw std::runtime_error("scripted failure");
        }
        return m_target->get([&](ScriptedTarget& t) {
            ++t.readMemoryCalls;
            if (!t.connected || t.chip().protectedChip) {
                return -1;
            }
            for (std::size_t i = 0; i < buffer.size(); ++i) {
                const std::uint8_t* byte = t.chip().readableAt(std::uint64_t{address} + i);
                if (byte == nullptr) {
                    return -1;
                }
                const bool mismatch = t.verifyMismatchAt && *t.verifyMismatchAt == address + i;
                buffer[i] = static_cast<std::byte>(mismatch ? (*byte ^ 0xFFU) : *byte);
            }
            return static_cast<int>(buffer.size());
        });
    }

    std::expected<std::uint32_t, int> readU32(std::uint32_t address) override {
        return m_target->get([&](ScriptedTarget& t) -> std::expected<std::uint32_t, int> {
            ScriptedChip& chip = t.chip();
            if (!t.connected || chip.protectedChip) {
                return std::unexpected(-1);
            }
            if (address == chip.ficrPart()) {
                return chip.part;
            }
            if (address == chip.ficrPart() + 4) {
                return 0x41414130U;
            }
            return std::unexpected(-1);
        });
    }

    int installUnsecureDialogHook() override {
        m_target->set([](ScriptedTarget& t) { ++t.unsecureHooks; });
        return 0;
    }

    int eraseChip() override {
        std::this_thread::sleep_for(
            m_target->get([](ScriptedTarget& t) { return t.eraseChipDelay; }));
        return m_target->get([](ScriptedTarget& t) {
            t.ops.emplace_back("eraseChip");
            if (!t.connected) {
                return -1;
            }
            t.chip().eraseAll();
            return 0;
        });
    }

    int coresightConfigure() override { return 0; }

    // CTRL-AP ERASEALLSTATUS: busy for eraseAllPolls reads, then the chip is erased.
    static std::uint32_t eraseAllStatus(ScriptedTarget& t) {
        if (t.eraseAllRunning && t.eraseAllLeft > 0) {
            --t.eraseAllLeft;
            return 1U;
        }
        if (t.eraseAllRunning) {
            t.eraseAllRunning = false;
            t.chip().eraseAll();
            t.chip().protectedChip = false;
            t.ops.emplace_back("eraseAll");
        }
        return 0U;
    }

    std::expected<std::uint32_t, int> coresightRead(std::uint8_t index, bool ap) override {
        return m_target->get([&](ScriptedTarget& t) -> std::expected<std::uint32_t, int> {
            if (!ap) {
                return index == 2 ? t.apSelect : 0U;
            }
            ScriptedChip& chip = t.chip();
            if ((t.apSelect >> 24U) != chip.ctrlAp()) {
                return std::unexpected(-1);
            }
            if (index == 2) {
                return eraseAllStatus(t);
            }
            if (index == 3 && ((t.apSelect >> 4U) & 0xFU) == 0xFU) {
                return chip.nrf91 ? 0x12880000U : 0x02880000U; // CTRL-AP IDR (bank 0xF)
            }
            if (index == 3) {
                return chip.protectedChip ? 0U : 1U;
            }
            return 0U;
        });
    }

    int coresightWrite(std::uint8_t index, bool ap, std::uint32_t value) override {
        return m_target->get([&](ScriptedTarget& t) {
            if (!ap) {
                if (index == 2) {
                    t.apSelect = value;
                }
                return 0;
            }
            if ((t.apSelect >> 24U) != t.chip().ctrlAp()) {
                return -1;
            }
            if (index == 1 && value == 1 && !t.eraseAllRunning) {
                t.eraseAllRunning = true;
                t.eraseAllLeft = t.eraseAllPolls;
            }
            return 0;
        });
    }

    int rttStart(std::optional<std::uint32_t> blockAddress) override {
        return m_target->get([blockAddress](ScriptedTarget& t) {
            ++t.rttStarts;
            t.rttStartAddresses.push_back(blockAddress);
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
