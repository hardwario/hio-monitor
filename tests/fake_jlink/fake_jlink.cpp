// Fake JLinkARM library for tests (plan/features/0002-m1-jlink-rtt.md, phase E1).
//
// A shared library that exports the J-Link C functions declared in libs/hw/jlink/src/jlink_abi.h
// and simulates probes, targets and RTT from a scenario file, so that the real locator, loader
// (one copy per session, ADR-0030), probe, RTT and session code run in CI without hardware.
// Test-only: never installed or shipped.
//
// The scenario is read from the JSON file named by the environment variable
// HIO_FAKE_JLINK_SCENARIO when the library copy is first used (QJsonDocument, ADR-0019). Without
// the variable (or with an unreadable file) the library has no probes. Each loaded copy has its
// own state; all copies read the same scenario, and each copy works with the probe it selects.
//
// Scenario format (all keys optional; times in ms):
//   {
//     "libraryVersion": 81000,                      JLINKARM_GetDLLVersion (8.10)
//     "compileDateTime": "Oct  1 2026 12:00:00",
//     "probes": [ {
//       "serialNumber": 801000001,
//       "product": "Test Probe",
//       "connection": "usb" | "ip",
//       "listFirmware": "",                         firmware string in EMU_GetList
//       "firmware": "J-Link V1 compiled Oct  1 2024 10:00:00",
//       "libraryFirmware": "...",                   GetEmbeddedFWString (default: firmware)
//       "openError": "",                            non-empty: JLINKARM_Open fails with it
//       "connectFailures": 0,                       the first N JLINKARM_Connect calls fail
//       "connectDelayMs": 0,                        every JLINKARM_Connect blocks this long
//       "connectMinMillivolts": 1000,               JLINKARM_Connect fails below this VTref
//       "vtref": [[0, 3300], [5000, 0]],            VTref steps [msSinceOpen, mV]
//       "rtt": {
//         "foundAfterMs": 0,                        control block found this long after boot
//         "staleUntilMs": 0,                        terminal (up 0) size reported 0 until then
//         "up":   [ {"name": "Terminal", "size": 1024, "registeredAfterMs": 0}, ... ],
//         "down": [ {"name": "Terminal", "size": 16} ],
//         "output": [ {"atMs": 0, "buffer": 0, "text": "line\n"} | {..., "hex": "c3a9"} ],
//         "stream": [ {"buffer": 1, "linesPerSecond": 5000, "text": "<inf> perf: line"} ],
//                                                   generated lines "<text> <n>\n" since boot
//                                                   (console performance test, plan 0005)
//         "responses": { "version": "Firmware version: v3.1.0\n" },  reply on up buffer 0
//         "responseDelayMs": 0,                     the reply comes this long after the write
//         "echo": false,                            echo written lines on up buffer 0
//         "writeStall": [[fromMs, toMs]],           RTT writes accept 0 bytes in these ranges
//         "acceptWritesAfterMs": 0,                 RTT writes accept 0 bytes until this long
//                                                   after the first write attempt of the copy
//         "acceptEachWriteAfterMs": 0,              every write (each chunk) accepts 0 bytes
//                                                   until this long after its own first attempt
//         "readErrors": [[fromMs, toMs]]            RTT reads fail in these ranges
//       },
//       "chip": {                                   flash memory model (plan/features/0003, E1)
//         "family": "nrf52" | "nrf91",              FICR / UICR / CTRL-AP layout (default nrf52)
//         "part": 337984, "variant": 1094796336,    FICR INFO.PART / VARIANT (default by family:
//                                                   0x52840 / 0x9120, variant "AAD0")
//         "protected": false,                       APPROTECT: memory access and connect fail
//         "memory": [{"address": 268439680, "hex": "babecafe..."}],  initial flash/UICR bytes
//         "verifyMismatchAt": 4096,                 reads of this address return the byte ^ 0xFF
//         "writeFault": false,                      JLINKARM_EndDownload fails
//         "eraseChipFails": false,                  JLINK_EraseChip fails
//         "eraseAllPolls": 2,                       CTRL-AP ERASEALLSTATUS reads "busy" N times
//         "downloadDelayMs": 0, "readDelayMs": 0    JLINKARM_EndDownload / every ReadMemEx blocks
//                                                   (CTRL-AP IDR in bank 0xF: nRF52 0x02880000,
//                                                   nRF91 0x12880000; other APs do not answer)
//         "absentApsReadZero": false                other AP indexes read 0 instead of failing
//                                                   (like a real DAP with an absent AP)
//                                                   JLINKARM_Connect on a protected chip asks the
//                                                   JLINK_SetHookUnsecureDialog hook "unsecure?":
//                                                   YES (or no hook: the batch-mode default, like
//                                                   the real library may do) mass-erases the chip
//                                                   incl. UICR and connects; NO fails the connect
//       } } ] }
// Memory: flash 0x0-0x100000 and the UICR page (nRF52: 0x10001000, nRF91: 0x00FF8000; 4 KiB) start
// erased (0xFF). A download (BeginDownload, WriteMem, EndDownload) behaves like the J-Link flash
// loader: only the written bytes change, the rest of every touched 4 KiB sector is kept; bytes
// outside flash and UICR make EndDownload fail. EndDownload reports "Erase" and "Program" progress
// to the JLINK_SetFlashProgProgressCallback callback. Like J-Link 9.84 on hardware (Phase 0 item
// 0.10, ADR-0109), JLINKARM_Close during a download programs the buffered bytes; and (HIL H3)
// JLINKARM_EMU_SelectByUSBSN fails while a flash progress callback is registered. JLINK_EraseChip
// and CTRL-AP ERASEALL erase
// flash and UICR; ERASEALL also clears "protected". The CTRL-AP is AP 1 (nRF52) / AP 4 (nRF91)
// with RESET (0x0), ERASEALL (0x4), ERASEALLSTATUS (0x8) and APPROTECTSTATUS (0xC).
// "Boot" is JLINKARM_Open and every JLINKARM_Reset (a reset restarts the RTT timeline: the
// control block must be found again and the output is replayed). VTref times count from the first
// JLINKARM_Open of the library copy (the target's power does not restart when the probe is
// reopened).

#include "jlink_abi.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QtEnvironmentVariables>
#include <QtTypes>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define HIO_FAKE_JLINK_EXPORT __declspec(dllexport)
#else
#define HIO_FAKE_JLINK_EXPORT __attribute__((visibility("default")))
#endif

namespace {

namespace abi = hio::jlink::abi;
using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::milliseconds;

// Bound of the simulated target-side data per up buffer (older bytes are dropped).
constexpr std::size_t maxPendingBytes = 1024ULL * 1024;

struct Range {
    std::int64_t from = 0;
    std::int64_t to = 0;
};

struct BufferSpec {
    std::string name;
    std::uint32_t size = 0;
    std::int64_t registeredAfterMs = 0;
};

struct OutputItem {
    std::int64_t atMs = 0;
    int buffer = 0;
    std::string bytes;
};

// A generated stream of lines (performance tests).
struct StreamSpec {
    int buffer = 0;
    std::int64_t linesPerSecond = 0;
    std::string text;
};

struct RttScenario {
    std::int64_t foundAfterMs = 0;
    std::int64_t staleUntilMs = 0;
    std::vector<BufferSpec> up;
    std::vector<BufferSpec> down;
    std::vector<OutputItem> output;
    std::vector<StreamSpec> streams;
    std::map<std::string, std::string> responses;
    std::int64_t responseDelayMs = 0;
    bool echo = false;
    std::vector<Range> writeStall;
    std::int64_t acceptWritesAfterMs = 0;
    std::int64_t acceptEachWriteAfterMs = 0;
    std::vector<Range> readErrors;
};

struct ChipScenario {
    bool nrf91 = false;
    std::uint32_t part = 0x52840;
    std::uint32_t variant = 0x41414430; // "AAD0"
    bool protectedChip = false;
    std::optional<std::uint32_t> verifyMismatchAt;
    bool writeFault = false;
    bool eraseChipFails = false;
    int eraseAllPolls = 2;
    std::int64_t downloadDelayMs = 0;
    bool absentApsReadZero = false;
    std::int64_t readDelayMs = 0;
    std::vector<std::uint8_t> flash = std::vector<std::uint8_t>(0x100000, 0xFF);
    std::vector<std::uint8_t> uicr = std::vector<std::uint8_t>(0x1000, 0xFF);

    [[nodiscard]] std::uint32_t uicrBase() const { return nrf91 ? 0x00FF8000U : 0x10001000U; }
    [[nodiscard]] std::uint32_t ficrPart() const { return nrf91 ? 0x00FF0140U : 0x10000100U; }
    [[nodiscard]] std::uint32_t ctrlAp() const { return nrf91 ? 4U : 1U; }

    // The byte at `address`, or nullopt outside flash and UICR.
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

struct ProbeScenario {
    std::uint32_t serialNumber = 0;
    std::string product;
    std::uint8_t connection = abi::connectionUsb;
    std::string listFirmware;
    std::string firmware;
    std::string libraryFirmware;
    std::string openError;
    int connectFailures = 0;
    std::int64_t connectDelayMs = 0;
    int connectMinMillivolts = 1000;
    std::vector<std::pair<std::int64_t, int>> vtref{{0, 3300}};
    RttScenario rtt;
    ChipScenario chip;
};

struct Scenario {
    std::uint32_t libraryVersion = 81000;
    std::string compileDateTime = "Oct  1 2026 12:00:00";
    std::vector<ProbeScenario> probes;
};

std::vector<Range> readRanges(const QJsonValue& value) {
    std::vector<Range> ranges;
    for (const QJsonValue item : value.toArray()) {
        const QJsonArray pair = item.toArray();
        ranges.push_back(Range{.from = pair.at(0).toInteger(), .to = pair.at(1).toInteger()});
    }
    return ranges;
}

std::vector<BufferSpec> readBuffers(const QJsonValue& value) {
    std::vector<BufferSpec> buffers;
    for (const QJsonValue item : value.toArray()) {
        const QJsonObject object = item.toObject();
        buffers.push_back(BufferSpec{
            .name = object.value(QStringLiteral("name")).toString().toStdString(),
            .size = static_cast<std::uint32_t>(object.value(QStringLiteral("size")).toInteger()),
            .registeredAfterMs = object.value(QStringLiteral("registeredAfterMs")).toInteger()});
    }
    return buffers;
}

RttScenario readRtt(const QJsonObject& object) {
    RttScenario rtt;
    rtt.foundAfterMs = object.value(QStringLiteral("foundAfterMs")).toInteger();
    rtt.staleUntilMs = object.value(QStringLiteral("staleUntilMs")).toInteger();
    rtt.up = object.contains(QStringLiteral("up"))
                 ? readBuffers(object.value(QStringLiteral("up")))
                 : std::vector<BufferSpec>{{.name = "Terminal", .size = 1024},
                                           {.name = "Logger", .size = 1024}};
    rtt.down = object.contains(QStringLiteral("down"))
                   ? readBuffers(object.value(QStringLiteral("down")))
                   : std::vector<BufferSpec>{{.name = "Terminal", .size = 16}};
    for (const QJsonValue item : object.value(QStringLiteral("output")).toArray()) {
        const QJsonObject output = item.toObject();
        const QByteArray bytes =
            output.contains(QStringLiteral("hex"))
                ? QByteArray::fromHex(output.value(QStringLiteral("hex")).toString().toLatin1())
                : output.value(QStringLiteral("text")).toString().toUtf8();
        rtt.output.push_back(OutputItem{.atMs = output.value(QStringLiteral("atMs")).toInteger(),
                                        .buffer = output.value(QStringLiteral("buffer")).toInt(),
                                        .bytes = bytes.toStdString()});
    }
    std::ranges::stable_sort(rtt.output, {}, &OutputItem::atMs);
    for (const QJsonValue item : object.value(QStringLiteral("stream")).toArray()) {
        const QJsonObject stream = item.toObject();
        rtt.streams.push_back(
            StreamSpec{.buffer = stream.value(QStringLiteral("buffer")).toInt(),
                       .linesPerSecond = stream.value(QStringLiteral("linesPerSecond")).toInteger(),
                       .text = stream.value(QStringLiteral("text")).toString().toStdString()});
    }
    const QJsonObject responses = object.value(QStringLiteral("responses")).toObject();
    for (auto it = responses.begin(); it != responses.end(); ++it) {
        rtt.responses.emplace(it.key().toStdString(), it.value().toString().toStdString());
    }
    rtt.echo = object.value(QStringLiteral("echo")).toBool();
    rtt.responseDelayMs = object.value(QStringLiteral("responseDelayMs")).toInteger();
    rtt.writeStall = readRanges(object.value(QStringLiteral("writeStall")));
    rtt.acceptWritesAfterMs = object.value(QStringLiteral("acceptWritesAfterMs")).toInteger();
    rtt.acceptEachWriteAfterMs = object.value(QStringLiteral("acceptEachWriteAfterMs")).toInteger();
    rtt.readErrors = readRanges(object.value(QStringLiteral("readErrors")));
    return rtt;
}

ChipScenario readChip(const QJsonObject& object) {
    ChipScenario chip;
    chip.nrf91 = object.value(QStringLiteral("family")).toString() == QStringLiteral("nrf91");
    if (chip.nrf91) {
        chip.part = 0x9120;
    }
    chip.part =
        static_cast<std::uint32_t>(object.value(QStringLiteral("part")).toInteger(chip.part));
    chip.variant =
        static_cast<std::uint32_t>(object.value(QStringLiteral("variant")).toInteger(chip.variant));
    chip.protectedChip = object.value(QStringLiteral("protected")).toBool();
    if (object.contains(QStringLiteral("verifyMismatchAt"))) {
        chip.verifyMismatchAt = static_cast<std::uint32_t>(
            object.value(QStringLiteral("verifyMismatchAt")).toInteger());
    }
    chip.writeFault = object.value(QStringLiteral("writeFault")).toBool();
    chip.eraseChipFails = object.value(QStringLiteral("eraseChipFails")).toBool();
    chip.eraseAllPolls = object.value(QStringLiteral("eraseAllPolls")).toInt(2);
    chip.downloadDelayMs = object.value(QStringLiteral("downloadDelayMs")).toInteger();
    chip.absentApsReadZero = object.value(QStringLiteral("absentApsReadZero")).toBool();
    chip.readDelayMs = object.value(QStringLiteral("readDelayMs")).toInteger();
    for (const QJsonValue item : object.value(QStringLiteral("memory")).toArray()) {
        const QJsonObject block = item.toObject();
        const auto address =
            static_cast<std::uint64_t>(block.value(QStringLiteral("address")).toInteger());
        const QByteArray bytes =
            QByteArray::fromHex(block.value(QStringLiteral("hex")).toString().toLatin1());
        for (qsizetype i = 0; i < bytes.size(); ++i) {
            if (std::uint8_t* byte = chip.at(address + static_cast<std::uint64_t>(i))) {
                *byte = static_cast<std::uint8_t>(bytes.at(i));
            }
        }
    }
    return chip;
}

Scenario loadScenario() {
    Scenario scenario;
    const QString path = qEnvironmentVariable("HIO_FAKE_JLINK_SCENARIO");
    if (path.isEmpty()) {
        return scenario;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return scenario;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.contains(QStringLiteral("libraryVersion"))) {
        scenario.libraryVersion =
            static_cast<std::uint32_t>(root.value(QStringLiteral("libraryVersion")).toInteger());
    }
    if (root.contains(QStringLiteral("compileDateTime"))) {
        scenario.compileDateTime =
            root.value(QStringLiteral("compileDateTime")).toString().toStdString();
    }
    for (const QJsonValue item : root.value(QStringLiteral("probes")).toArray()) {
        const QJsonObject object = item.toObject();
        ProbeScenario probe;
        probe.serialNumber =
            static_cast<std::uint32_t>(object.value(QStringLiteral("serialNumber")).toInteger());
        probe.product = object.value(QStringLiteral("product")).toString().toStdString();
        probe.connection =
            object.value(QStringLiteral("connection")).toString() == QStringLiteral("ip")
                ? abi::connectionIp
                : abi::connectionUsb;
        probe.listFirmware = object.value(QStringLiteral("listFirmware")).toString().toStdString();
        probe.firmware = object.value(QStringLiteral("firmware"))
                             .toString(QStringLiteral("J-Link V1 compiled Oct  1 2024 10:00:00"))
                             .toStdString();
        probe.libraryFirmware =
            object.contains(QStringLiteral("libraryFirmware"))
                ? object.value(QStringLiteral("libraryFirmware")).toString().toStdString()
                : probe.firmware;
        probe.openError = object.value(QStringLiteral("openError")).toString().toStdString();
        probe.connectFailures = object.value(QStringLiteral("connectFailures")).toInt();
        probe.connectDelayMs = object.value(QStringLiteral("connectDelayMs")).toInteger();
        probe.connectMinMillivolts =
            object.value(QStringLiteral("connectMinMillivolts")).toInt(1000);
        if (object.contains(QStringLiteral("vtref"))) {
            probe.vtref.clear();
            for (const QJsonValue step : object.value(QStringLiteral("vtref")).toArray()) {
                const QJsonArray pair = step.toArray();
                probe.vtref.emplace_back(pair.at(0).toInteger(), pair.at(1).toInt());
            }
        }
        probe.rtt = readRtt(object.value(QStringLiteral("rtt")).toObject());
        probe.chip = readChip(object.value(QStringLiteral("chip")).toObject());
        scenario.probes.push_back(std::move(probe));
    }
    return scenario;
}

bool inRanges(const std::vector<Range>& ranges, std::int64_t at) {
    return std::ranges::any_of(ranges, [at](const Range& r) { return at >= r.from && at < r.to; });
}

void copyText(std::string_view text, std::span<char> out) {
    if (out.empty()) {
        return;
    }
    const std::size_t length = std::min(text.size(), out.size() - 1);
    std::ranges::copy(text.substr(0, length), out.begin());
    out[length] = '\0';
}

// The state of this library copy. Every exported function takes `mutex`.
struct State {
    std::mutex mutex;
    std::optional<Scenario> scenario; // loaded on first use
    std::optional<std::size_t> selected;
    bool open = false;
    bool connected = false;
    bool rttStarted = false;
    bool everOpened = false;
    int connectCalls = 0;
    Clock::time_point openedAt;
    Clock::time_point bootedAt;
    std::optional<Clock::time_point> firstWriteAt; // acceptWritesAfterMs counts from here
    // acceptEachWriteAfterMs counts from here; reset when a write is accepted.
    std::optional<Clock::time_point> pendingWriteSince;
    std::size_t nextOutput = 0;              // next item of rtt.output since boot
    std::vector<std::int64_t> streamLines;   // lines generated per rtt.streams item since boot
    std::map<int, std::deque<char>> pending; // target-side data per up buffer
    std::string writtenLine;                 // down buffer 0 until '\n'
    // Replies due later (responseDelayMs); bounded by the lines written (tests only).
    std::deque<std::pair<Clock::time_point, std::string>> delayedReplies;
    abi::LogHandler logHandler = nullptr;
    abi::LogHandler warnHandler = nullptr;
    abi::LogHandler errorHandler = nullptr;
    abi::FlashProgressCallback flashProgress = nullptr;
    abi::UnsecureDialogHook unsecureHook = nullptr;
    // Flash download (BeginDownload .. EndDownload): the buffered writes, bounded by the tests.
    bool downloading = false;
    std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> downloadData;
    // CoreSight: DP SELECT and the CTRL-AP erase-all state.
    bool coresightConfigured = false;
    std::uint32_t apSelect = 0;
    int eraseAllPollsLeft = 0;
    bool eraseAllRunning = false;
    std::uint32_t ctrlApReset = 0;
    std::string lastOpenError; // keeps the text returned by JLINKARM_Open alive

    Scenario& data() {
        if (!scenario) {
            scenario = loadScenario();
        }
        return *scenario;
    }

    ProbeScenario* probe() {
        const std::size_t index = selected.value_or(data().probes.size());
        return index < data().probes.size() ? &data().probes.at(index) : nullptr;
    }

    [[nodiscard]] std::int64_t sinceOpen() const {
        return std::chrono::duration_cast<Milliseconds>(Clock::now() - openedAt).count();
    }

    [[nodiscard]] std::int64_t sinceBoot() const {
        return std::chrono::duration_cast<Milliseconds>(Clock::now() - bootedAt).count();
    }

    int vtref() {
        const ProbeScenario* p = probe();
        if (p == nullptr) {
            return 0;
        }
        int value = 0;
        const std::int64_t now = sinceOpen();
        for (const auto& [at, millivolts] : p->vtref) {
            if (at <= now) {
                value = millivolts;
            }
        }
        return value;
    }

    void boot() {
        bootedAt = Clock::now();
        nextOutput = 0;
        streamLines.clear();
        pending.clear();
        delayedReplies.clear();
        writtenLine.clear();
        rttStarted = false;
    }

    static void report(abi::LogHandler handler, const std::string& text) {
        if (handler != nullptr) {
            handler(text.c_str());
        }
    }

    // Programs the buffered download bytes that lie in flash or UICR and clears the buffer. The
    // loader keeps the unwritten bytes of every touched sector (read-modify-write). Returns the
    // number of bytes programmed.
    std::size_t programBuffered(ChipScenario& chip) {
        std::size_t total = 0;
        for (const auto& [address, bytes] : downloadData) {
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                if (std::uint8_t* byte = chip.at(std::uint64_t{address} + i); byte != nullptr) {
                    *byte = bytes[i];
                    ++total;
                }
            }
        }
        downloadData.clear();
        return total;
    }

    void appendPending(int buffer, std::string_view bytes) {
        std::deque<char>& queue = pending[buffer];
        queue.insert(queue.end(), bytes.begin(), bytes.end());
        while (queue.size() > maxPendingBytes) {
            queue.pop_front();
        }
    }

    // Moves the output items that are due into the pending buffers.
    void advanceOutput() {
        ProbeScenario* p = probe();
        if (p == nullptr) {
            return;
        }
        while (!delayedReplies.empty() && delayedReplies.front().first <= Clock::now()) {
            appendPending(0, delayedReplies.front().second);
            delayedReplies.pop_front();
        }
        const std::int64_t now = sinceBoot();
        while (nextOutput < p->rtt.output.size() && p->rtt.output.at(nextOutput).atMs <= now) {
            const OutputItem& item = p->rtt.output.at(nextOutput);
            appendPending(item.buffer, item.bytes);
            ++nextOutput;
        }
        streamLines.resize(p->rtt.streams.size(), 0);
        for (std::size_t i = 0; i < p->rtt.streams.size(); ++i) {
            const StreamSpec& stream = p->rtt.streams[i];
            const std::int64_t due = now * stream.linesPerSecond / 1000;
            std::string bytes;
            // At most the pending bound per call (older target-side data is dropped anyway).
            while (streamLines[i] < due && bytes.size() < maxPendingBytes) {
                bytes += stream.text + " " + std::to_string(streamLines[i]++) + "\n";
            }
            streamLines[i] = std::max(streamLines[i], due);
            if (!bytes.empty()) {
                appendPending(stream.buffer, bytes);
            }
        }
    }

    [[nodiscard]] bool controlBlockFound() {
        const ProbeScenario* p = probe();
        return p != nullptr && rttStarted && sinceBoot() >= p->rtt.foundAfterMs;
    }

    // Registered buffers in one direction (registered in order, like firmware does at boot).
    [[nodiscard]] int registeredCount(const std::vector<BufferSpec>& buffers) const {
        int count = 0;
        const std::int64_t now = sinceBoot();
        for (const BufferSpec& buffer : buffers) {
            if (buffer.registeredAfterMs > now) {
                break;
            }
            ++count;
        }
        return count;
    }
};

State& state() {
    static State instance;
    return instance;
}

// A slow vendor call: sleeps outside the lock.
void chipDelay(std::int64_t ChipScenario::* field) {
    std::int64_t delay = 0;
    {
        State& s = state();
        const std::scoped_lock lock(s.mutex);
        if (const ProbeScenario* probe = s.probe()) {
            delay = probe->chip.*field;
        }
    }
    if (delay > 0) {
        std::this_thread::sleep_for(Milliseconds(delay));
    }
}

} // namespace

extern "C" {

HIO_FAKE_JLINK_EXPORT std::uint32_t JLINKARM_GetDLLVersion() {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    return s.data().libraryVersion;
}

HIO_FAKE_JLINK_EXPORT const char* JLINKARM_GetCompileDateTime() {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    return s.data().compileDateTime.c_str();
}

HIO_FAKE_JLINK_EXPORT int JLINKARM_EMU_GetList(int hosts, abi::EmuConnectInfo* infos,
                                               int maxInfos) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    const auto& probes = s.data().probes;
    std::vector<const ProbeScenario*> matching;
    for (const ProbeScenario& probe : probes) {
        const int host = probe.connection == abi::connectionIp ? abi::hostIp : abi::hostUsb;
        if ((hosts & host) != 0) {
            matching.push_back(&probe);
        }
    }
    if (infos == nullptr || maxInfos <= 0) {
        return static_cast<int>(matching.size());
    }
    const std::span<abi::EmuConnectInfo> out(infos, static_cast<std::size_t>(maxInfos));
    const std::size_t count = std::min(out.size(), matching.size());
    for (std::size_t i = 0; i < count; ++i) {
        abi::EmuConnectInfo info{};
        info.serialNumber = matching[i]->serialNumber;
        info.connection = matching[i]->connection;
        copyText(matching[i]->product, info.product);
        copyText(matching[i]->listFirmware, info.firmwareString);
        out[i] = info;
    }
    return static_cast<int>(count);
}

HIO_FAKE_JLINK_EXPORT int JLINKARM_EMU_SelectByUSBSN(std::uint32_t serialNumber) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    const auto& probes = s.data().probes;
    const auto it = std::ranges::find(probes, serialNumber, &ProbeScenario::serialNumber);
    // HIL H3: with the progress callback registered before the select, the real library's select
    // failed (-1).
    if (it == probes.end() || s.flashProgress != nullptr) {
        return -1;
    }
    s.selected = static_cast<std::size_t>(it - probes.begin());
    return 0;
}

HIO_FAKE_JLINK_EXPORT const char* JLINKARM_Open() {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    if (!s.selected && !s.data().probes.empty()) {
        s.selected = 0;
    }
    const ProbeScenario* probe = s.probe();
    if (probe == nullptr) {
        s.lastOpenError = "Cannot connect to J-Link.";
        State::report(s.errorHandler, s.lastOpenError);
        return s.lastOpenError.c_str();
    }
    if (!probe->openError.empty()) {
        s.lastOpenError = probe->openError;
        State::report(s.errorHandler, s.lastOpenError);
        return s.lastOpenError.c_str();
    }
    s.open = true;
    s.connected = false;
    if (!s.everOpened) {
        s.everOpened = true;
        s.openedAt = Clock::now();
    }
    s.boot();
    State::report(s.logHandler, "Fake J-Link " + std::to_string(probe->serialNumber) + " opened");
    return nullptr;
}

HIO_FAKE_JLINK_EXPORT void JLINKARM_Close() {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    if (s.downloading) { // Phase 0 item 0.10 (ADR-0109): the close programs the buffer
        s.downloading = false;
        if (ProbeScenario* probe = s.probe(); probe != nullptr) {
            s.programBuffered(probe->chip);
        }
    }
    s.open = false;
    s.connected = false;
    s.rttStarted = false;
}

HIO_FAKE_JLINK_EXPORT char JLINKARM_IsOpen() {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    return s.open ? 1 : 0;
}

HIO_FAKE_JLINK_EXPORT int JLINKARM_ExecCommand(const char* command, char* output, int size) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    if (output != nullptr && size > 0) {
        copyText({}, std::span<char>(output, static_cast<std::size_t>(size)));
    }
    if (command != nullptr && std::string_view(command) == "InvalidateFW") {
        State::report(s.logHandler, "Firmware invalidated");
    }
    return 0;
}

HIO_FAKE_JLINK_EXPORT int JLINKARM_TIF_Select(int /*targetInterface*/) { return 0; }

HIO_FAKE_JLINK_EXPORT void JLINKARM_SetSpeed(std::uint32_t /*speed*/) {}

HIO_FAKE_JLINK_EXPORT int JLINKARM_Connect() {
    std::int64_t delay = 0;
    {
        State& s = state();
        const std::scoped_lock lock(s.mutex);
        if (const ProbeScenario* probe = s.probe()) {
            delay = probe->connectDelayMs;
        }
    }
    if (delay > 0) {
        std::this_thread::sleep_for(Milliseconds(delay)); // a slow vendor call, outside the lock
    }
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    ProbeScenario* probe = s.probe();
    if (!s.open || probe == nullptr) {
        return -1;
    }
    ++s.connectCalls;
    if (probe->chip.protectedChip && s.connectCalls > probe->connectFailures &&
        s.vtref() >= probe->connectMinMillivolts) {
        const int answer = s.unsecureHook != nullptr
                               ? s.unsecureHook("J-Link V9 (fake)", "Unsecure the device?", 0x3U)
                               : abi::dlgButtonYes;
        if (answer == abi::dlgButtonYes) {
            State::report(s.warnHandler, "Device unsecured (mass erase)");
            probe->chip.eraseAll();
            probe->chip.protectedChip = false;
        }
    }
    if (s.connectCalls <= probe->connectFailures || s.vtref() < probe->connectMinMillivolts ||
        probe->chip.protectedChip) {
        s.connected = false;
        State::report(s.errorHandler, "Could not connect to target.");
        return -1;
    }
    s.connected = true;
    return 0;
}

HIO_FAKE_JLINK_EXPORT char JLINKARM_IsConnected() {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    return s.connected ? 1 : 0;
}

HIO_FAKE_JLINK_EXPORT int JLINKARM_GetHWStatus(abi::HwStatus* status) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    if (!s.open || status == nullptr) {
        return 1;
    }
    *status = abi::HwStatus{};
    status->vTarget = static_cast<std::uint16_t>(s.vtref());
    return 0;
}

HIO_FAKE_JLINK_EXPORT void JLINKARM_GetFirmwareString(char* buffer, int size) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    if (buffer == nullptr || size <= 0) {
        return;
    }
    const ProbeScenario* probe = s.probe();
    copyText(probe != nullptr && s.open ? probe->firmware : std::string(),
             std::span<char>(buffer, static_cast<std::size_t>(size)));
}

HIO_FAKE_JLINK_EXPORT int JLINKARM_GetEmbeddedFWString(const char* /*id*/, char* buffer,
                                                       std::uint32_t size) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    const ProbeScenario* probe = s.probe();
    if (probe == nullptr || buffer == nullptr || size == 0) {
        return -1;
    }
    copyText(probe->libraryFirmware, std::span<char>(buffer, size));
    return 0;
}

HIO_FAKE_JLINK_EXPORT std::uint32_t JLINKARM_UpdateFirmwareIfNewer() {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    ProbeScenario* probe = s.probe();
    if (probe == nullptr || probe->firmware == probe->libraryFirmware) {
        return 0;
    }
    probe->firmware = probe->libraryFirmware;
    return 0x1234ABCDU;
}

HIO_FAKE_JLINK_EXPORT int JLINKARM_GetSN() {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    const ProbeScenario* probe = s.probe();
    return probe != nullptr ? static_cast<int>(probe->serialNumber) : -1;
}

HIO_FAKE_JLINK_EXPORT void JLINKARM_SetResetDelay(int /*milliseconds*/) {}

HIO_FAKE_JLINK_EXPORT int JLINKARM_Reset() {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    if (!s.connected) {
        return -1;
    }
    s.boot();
    return 0;
}

HIO_FAKE_JLINK_EXPORT char JLINKARM_Halt() {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    return s.connected ? 0 : 1;
}

HIO_FAKE_JLINK_EXPORT void JLINKARM_Go() {}

HIO_FAKE_JLINK_EXPORT void JLINKARM_EnableLog(abi::LogHandler handler) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    s.logHandler = handler;
}

HIO_FAKE_JLINK_EXPORT void JLINKARM_SetWarnOutHandler(abi::LogHandler handler) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    s.warnHandler = handler;
}

HIO_FAKE_JLINK_EXPORT void JLINKARM_SetErrorOutHandler(abi::LogHandler handler) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    s.errorHandler = handler;
}

HIO_FAKE_JLINK_EXPORT int HIO_JLINK_STDCALL JLINK_RTTERMINAL_Control(std::uint32_t command,
                                                                     void* config) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    ProbeScenario* probe = s.probe();
    if (!s.connected || probe == nullptr) {
        return -1;
    }
    switch (command) {
    case abi::rttCmdStart:
        s.rttStarted = true;
        return 0;
    case abi::rttCmdStop:
        s.rttStarted = false;
        return 0;
    case abi::rttCmdGetNumBuf: {
        if (!s.controlBlockFound() || config == nullptr) {
            return -2;
        }
        const std::uint32_t direction = *static_cast<const std::uint32_t*>(config);
        return s.registeredCount(direction == abi::rttDirectionUp ? probe->rtt.up
                                                                  : probe->rtt.down);
    }
    case abi::rttCmdGetDesc: {
        if (!s.controlBlockFound() || config == nullptr) {
            return -2;
        }
        auto* desc = static_cast<abi::RttBufDesc*>(config);
        const bool up = desc->direction == abi::rttDirectionUp;
        const auto& buffers = up ? probe->rtt.up : probe->rtt.down;
        const int count = s.registeredCount(buffers);
        if (desc->bufferIndex < 0 || desc->bufferIndex >= count) {
            return -1;
        }
        const BufferSpec& buffer = buffers.at(static_cast<std::size_t>(desc->bufferIndex));
        copyText(buffer.name, desc->name);
        desc->sizeOfBuffer = buffer.size;
        if (up && desc->bufferIndex == 0 && s.sinceBoot() < probe->rtt.staleUntilMs) {
            desc->sizeOfBuffer = 0; // stale control block left in RAM
        }
        desc->flags = 0;
        return 0;
    }
    default:
        return -1;
    }
}

HIO_FAKE_JLINK_EXPORT int HIO_JLINK_STDCALL JLINK_RTTERMINAL_Read(std::uint32_t index, char* buffer,
                                                                  std::uint32_t size) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    const ProbeScenario* probe = s.probe();
    if (!s.rttStarted || probe == nullptr || buffer == nullptr) {
        return -1;
    }
    if (inRanges(probe->rtt.readErrors, s.sinceBoot())) {
        State::report(s.errorHandler, "RTT read failed (simulated)");
        return -1;
    }
    if (!s.controlBlockFound()) {
        return 0;
    }
    s.advanceOutput();
    std::deque<char>& queue = s.pending[static_cast<int>(index)];
    const std::size_t count = std::min<std::size_t>(size, queue.size());
    const std::span<char> out(buffer, count);
    std::copy_n(queue.begin(), count, out.begin());
    queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(count));
    return static_cast<int>(count);
}

HIO_FAKE_JLINK_EXPORT int HIO_JLINK_STDCALL JLINK_RTTERMINAL_Write(std::uint32_t index,
                                                                   const char* data,
                                                                   std::uint32_t size) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    ProbeScenario* probe = s.probe();
    if (!s.rttStarted || probe == nullptr || data == nullptr || !s.controlBlockFound()) {
        return -1;
    }
    if (inRanges(probe->rtt.writeStall, s.sinceBoot())) {
        return 0;
    }
    if (!s.firstWriteAt) {
        s.firstWriteAt = Clock::now();
    }
    if (Clock::now() - *s.firstWriteAt < Milliseconds(probe->rtt.acceptWritesAfterMs)) {
        return 0;
    }
    if (probe->rtt.acceptEachWriteAfterMs > 0) {
        if (!s.pendingWriteSince) {
            s.pendingWriteSince = Clock::now();
        }
        if (Clock::now() - *s.pendingWriteSince < Milliseconds(probe->rtt.acceptEachWriteAfterMs)) {
            return 0;
        }
        s.pendingWriteSince.reset();
    }
    const std::size_t downSize =
        index < probe->rtt.down.size() ? probe->rtt.down.at(index).size : 0;
    const std::size_t accepted = std::min<std::size_t>(size, downSize);
    const std::string_view bytes(data, accepted);
    for (const char c : bytes) {
        if (c != '\n') {
            s.writtenLine += c;
            continue;
        }
        if (probe->rtt.echo) {
            s.appendPending(0, s.writtenLine + "\n");
        }
        const auto response = probe->rtt.responses.find(s.writtenLine);
        if (response != probe->rtt.responses.end() && probe->rtt.responseDelayMs > 0) {
            s.delayedReplies.emplace_back(Clock::now() + Milliseconds(probe->rtt.responseDelayMs),
                                          response->second);
        } else if (response != probe->rtt.responses.end()) {
            s.appendPending(0, response->second);
        }
        s.writtenLine.clear();
    }
    return static_cast<int>(accepted);
}

// ---- M2: memory, flash download, erase, CoreSight (plan/features/0003, E1) ----------------------

HIO_FAKE_JLINK_EXPORT void JLINK_SetFlashProgProgressCallback(abi::FlashProgressCallback callback) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    s.flashProgress = callback;
}

HIO_FAKE_JLINK_EXPORT int HIO_JLINK_STDCALL
JLINK_SetHookUnsecureDialog(abi::UnsecureDialogHook hook) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    s.unsecureHook = hook;
    return 0;
}

HIO_FAKE_JLINK_EXPORT void JLINKARM_BeginDownload(std::uint32_t /*flags*/) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    s.downloading = true;
    s.downloadData.clear();
}

HIO_FAKE_JLINK_EXPORT int JLINKARM_WriteMem(std::uint32_t address, std::uint32_t count,
                                            const void* data) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    if (!s.connected || data == nullptr || !s.downloading) {
        return -1;
    }
    const auto bytes = std::span(static_cast<const std::uint8_t*>(data), count);
    s.downloadData.emplace_back(address, std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    return static_cast<int>(count);
}

HIO_FAKE_JLINK_EXPORT int JLINKARM_EndDownload() {
    chipDelay(&ChipScenario::downloadDelayMs);
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    ProbeScenario* probe = s.probe();
    const bool wasDownloading = s.downloading;
    s.downloading = false;
    if (!s.connected || probe == nullptr || !wasDownloading) {
        return -1;
    }
    ChipScenario& chip = probe->chip;
    // Every byte must lie in flash or UICR (the loader has no other flash bank).
    for (const auto& [address, bytes] : s.downloadData) {
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            if (chip.at(std::uint64_t{address} + i) == nullptr) {
                State::report(s.errorHandler, "No flash bank at the download address");
                return -1;
            }
        }
    }
    if (chip.writeFault) {
        State::report(s.errorHandler, "Programming failed (simulated write fault)");
        return -1;
    }
    if (s.flashProgress != nullptr) {
        s.flashProgress("Erase", "", 0);
        s.flashProgress("Erase", "", 100);
        s.flashProgress("Program", "", 50);
        s.flashProgress("Program", "", 100);
    }
    const std::size_t total = s.programBuffered(chip);
    return static_cast<int>(std::min<std::size_t>(total, 0x7FFFFFFF));
}

HIO_FAKE_JLINK_EXPORT int JLINKARM_ReadMemEx(std::uint32_t address, std::uint32_t count, void* data,
                                             std::uint32_t /*flags*/) {
    chipDelay(&ChipScenario::readDelayMs);
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    ProbeScenario* probe = s.probe();
    if (!s.connected || probe == nullptr || data == nullptr || probe->chip.protectedChip) {
        return -1;
    }
    const auto out = std::span(static_cast<std::uint8_t*>(data), count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* byte = probe->chip.at(std::uint64_t{address} + i);
        if (byte == nullptr) {
            return -1;
        }
        const bool mismatch =
            probe->chip.verifyMismatchAt && *probe->chip.verifyMismatchAt == address + i;
        out[i] = mismatch ? static_cast<std::uint8_t>(*byte ^ 0xFFU) : *byte;
    }
    return static_cast<int>(count);
}

HIO_FAKE_JLINK_EXPORT int JLINKARM_ReadMemU32(std::uint32_t address, std::uint32_t count,
                                              std::uint32_t* data, std::uint8_t* status) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    ProbeScenario* probe = s.probe();
    if (!s.connected || probe == nullptr || data == nullptr || count != 1 ||
        probe->chip.protectedChip) {
        return -1;
    }
    const ChipScenario& chip = probe->chip;
    std::optional<std::uint32_t> value;
    if (address == chip.ficrPart()) {
        value = chip.part;
    } else if (address == chip.ficrPart() + 4) {
        value = chip.variant;
    }
    if (!value) {
        return -1; // unmapped (e.g. the FICR address of the other family)
    }
    *data = *value;
    if (status != nullptr) {
        *status = 0;
    }
    return 1;
}

HIO_FAKE_JLINK_EXPORT int HIO_JLINK_STDCALL JLINK_EraseChip() {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    ProbeScenario* probe = s.probe();
    if (!s.connected || probe == nullptr || probe->chip.eraseChipFails ||
        probe->chip.protectedChip) {
        State::report(s.errorHandler, "Erase failed (simulated)");
        return -1;
    }
    probe->chip.eraseAll();
    return 0;
}

HIO_FAKE_JLINK_EXPORT int JLINKARM_CORESIGHT_Configure(const char* /*config*/) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    if (!s.open) {
        return -1;
    }
    s.coresightConfigured = true;
    return 0;
}

HIO_FAKE_JLINK_EXPORT int JLINKARM_CORESIGHT_ReadAPDPReg(std::uint8_t index, std::uint8_t ap,
                                                         std::uint32_t* data) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    ProbeScenario* probe = s.probe();
    if (!s.open || !s.coresightConfigured || probe == nullptr || data == nullptr) {
        return -1;
    }
    if (ap == 0) {
        *data = index == 2 ? s.apSelect : 0;
        return 0;
    }
    ChipScenario& chip = probe->chip;
    if ((s.apSelect >> 24U) != chip.ctrlAp()) {
        if (chip.absentApsReadZero) {
            *data = 0;
            return 0;
        }
        return -1; // only the CTRL-AP is modelled
    }
    switch (index) {
    case 0:
        *data = s.ctrlApReset;
        return 0;
    case 2: // ERASEALLSTATUS
        if (s.eraseAllRunning && s.eraseAllPollsLeft > 0) {
            --s.eraseAllPollsLeft;
            *data = 1;
            return 0;
        }
        if (s.eraseAllRunning) {
            s.eraseAllRunning = false;
            chip.eraseAll();
            chip.protectedChip = false;
        }
        *data = 0;
        return 0;
    case 3: // bank 0xF: IDR; bank 0: APPROTECTSTATUS (bit 0, and bit 1 on nRF91, = 0: protected)
        if (((s.apSelect >> 4U) & 0xFU) == 0xFU) {
            *data = chip.nrf91 ? 0x12880000U : 0x02880000U;
            return 0;
        }
        if (chip.protectedChip) {
            *data = 0U;
        } else {
            *data = chip.nrf91 ? 3U : 1U;
        }
        return 0;
    default:
        *data = 0;
        return 0;
    }
}

HIO_FAKE_JLINK_EXPORT int JLINKARM_CORESIGHT_WriteAPDPReg(std::uint8_t index, std::uint8_t ap,
                                                          std::uint32_t value) {
    State& s = state();
    const std::scoped_lock lock(s.mutex);
    ProbeScenario* probe = s.probe();
    if (!s.open || !s.coresightConfigured || probe == nullptr) {
        return -1;
    }
    if (ap == 0) {
        if (index == 2) {
            s.apSelect = value;
        }
        return 0;
    }
    if ((s.apSelect >> 24U) != probe->chip.ctrlAp()) {
        return -1;
    }
    if (index == 1 && value == 1 && !s.eraseAllRunning) {
        s.eraseAllRunning = true;
        s.eraseAllPollsLeft = probe->chip.eraseAllPolls;
    }
    if (index == 0) {
        if (s.ctrlApReset == 1 && value == 0) {
            s.boot(); // the system reset ends: the firmware starts again
        }
        s.ctrlApReset = value;
    }
    return 0;
}

} // extern "C"
