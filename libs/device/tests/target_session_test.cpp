// M6 low-level target operations of DeviceSession (plan/features/0013, B2/B3; ADR-0099,
// ADR-0157) with the in-process scripted J-Link: RTT stays attached for the light operations on
// the console target, request-time range checks, writeFlash, releaseProbe / reacquireProbe, the
// sticky RTT control block address, RTT prompt detection, consoleFileDir.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/device_session.h"
#include "hio/device/operation.h"
#include "hio/device/probe_lock.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/device/session_registry.h"
#include "hio/firmware/program_progress.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/programmer.h"
#include "scripted_api.h"
#include "session_internal.h"
#include "session_test_support.h"

#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hio::ErrorCode;
using hio::device::AccessWidth;
using hio::device::Channel;
using hio::device::ConnectionState;
using hio::device::DeviceSession;
using hio::device::Direction;
using hio::device::OperationId;
using hio::device::OperationKind;
using hio::device::OperationResult;
using hio::device::SessionOptions;
using hio::device::SessionRegistry;
using hio::device::detail::OpenHooks;
using hio::device::test::Recorder;
using hio::device::test::ScriptedApi;
using hio::device::test::ScriptedTarget;
using hio::device::test::waitFor;
using hio::firmware::ProgramPhase;

constexpr std::uint32_t probeSn = 801000001;

// A CHESTER copy whose app target has an RTT shell prompt (ADR-0104; CHESTER's is empty).
const std::array<hio::device::TargetProfile, 2> promptTargets = [] {
    const auto* chester = hio::device::ProfileRegistry::find("chester");
    std::array<hio::device::TargetProfile, 2> targets{chester->targets[0], chester->targets[1]};
    targets[0].shellPrompt = "uart:~$ ";
    return targets;
}();
const hio::device::DeviceProfile promptProfile = [] {
    hio::device::DeviceProfile profile = *hio::device::ProfileRegistry::find("chester");
    profile.targets = promptTargets;
    return profile;
}();

std::vector<std::byte> bytesOf(std::initializer_list<unsigned> values) {
    std::vector<std::byte> bytes;
    for (const unsigned value : values) {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

class TargetOperations : public ::testing::Test {
protected:
    TargetOperations()
        : m_registry(std::make_unique<SessionRegistry>(
              hio::device::RegistryOptions{.instanceRoot = root() / "instances",
                                           .lockDirectory = root() / "locks",
                                           .consoleDirectory = root() / "console"})) {}

    [[nodiscard]] fs::path root() const { return {m_dir.path().toStdString()}; }
    [[nodiscard]] SessionRegistry& registry() { return *m_registry; }

    [[nodiscard]] static SessionOptions options(std::string_view target = "app",
                                                bool attachRtt = true) {
        SessionOptions options;
        options.profile = hio::device::ProfileRegistry::find("chester");
        options.targetId = target;
        options.probeSn = probeSn;
        options.speedKhz = 2000;
        options.latencyMs = 5;
        options.attachRtt = attachRtt;
        options.consoleFile = fs::path();
        return options;
    }

    [[nodiscard]] OpenHooks hooks() const {
        OpenHooks hooks;
        hooks.apiFactory = [target =
                                m_target]() -> hio::Result<std::unique_ptr<hio::jlink::IJLinkApi>> {
            return std::make_unique<ScriptedApi>(target);
        };
        hooks.timing.powerCheckInterval = std::chrono::milliseconds(20);
        hooks.timing.reconnectInterval = std::chrono::milliseconds(60);
        hooks.timing.probe.connectRetryDelay = std::chrono::milliseconds(5);
        hooks.timing.rtt.attachDeadline = std::chrono::milliseconds(5000);
        hooks.timing.rtt.pollInterval = std::chrono::milliseconds(5);
        hooks.timing.rtt.loggerWait = std::chrono::milliseconds(50);
        hooks.timing.rtt.staleRetryDelay = std::chrono::milliseconds(5);
        hooks.timing.rtt.writeTimeout = std::chrono::milliseconds(100);
        return hooks;
    }

    std::unique_ptr<DeviceSession> openConnected(const SessionOptions& opts) {
        auto session = hio::device::detail::openSession(registry(), hio::jlink::LibraryLocation{},
                                                        opts, hooks());
        EXPECT_TRUE(session) << session.error().message;
        if (!session) {
            return nullptr;
        }
        m_events = std::make_unique<Recorder>(**session);
        EXPECT_TRUE(waitFor([&] { return m_events->reached(ConnectionState::Connected); }));
        return std::move(*session);
    }

    // Opens a session and waits until it reports `state` (e.g. Stopped after a fatal first
    // connect).
    std::unique_ptr<DeviceSession> openIn(const SessionOptions& opts, ConnectionState state) {
        auto session = hio::device::detail::openSession(registry(), hio::jlink::LibraryLocation{},
                                                        opts, hooks());
        EXPECT_TRUE(session) << session.error().message;
        if (!session) {
            return nullptr;
        }
        m_events = std::make_unique<Recorder>(**session);
        EXPECT_TRUE(waitFor([&] { return m_events->reached(state); }));
        return std::move(*session);
    }

    const OperationResult* finish(const hio::Result<OperationId>& id) {
        EXPECT_TRUE(id) << (id ? std::string() : id.error().message);
        if (!id) {
            return nullptr;
        }
        EXPECT_TRUE(waitFor([&] { return events().result(*id) != nullptr; }));
        return events().result(*id);
    }

    // The shell lines received so far contain `text`; waits for it.
    bool receives(std::string_view text) {
        return waitFor([&] {
            const auto lines = events().texts(Channel::Shell, Direction::Out);
            return std::ranges::find(lines, text) != lines.end();
        });
    }

    [[nodiscard]] Recorder& events() { return *m_events; }
    [[nodiscard]] ScriptedTarget& target() { return *m_target; }
    [[nodiscard]] int rttStarts() {
        return target().get([](ScriptedTarget& t) { return t.rttStarts; });
    }
    [[nodiscard]] int rttStops() {
        return target().get([](ScriptedTarget& t) { return t.rttStops; });
    }

private:
    QTemporaryDir m_dir;
    std::shared_ptr<ScriptedTarget> m_target = std::make_shared<ScriptedTarget>();
    std::unique_ptr<SessionRegistry> m_registry;
    std::unique_ptr<Recorder> m_events;
};

// ADR-0157: halt keeps RTT attached and the console Connected; lines keep arriving; go too.
TEST_F(TargetOperations, HaltAndGoKeepRttAttached) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    const int starts = rttStarts();
    const int stops = rttStops();
    const std::size_t connections = events().connections.size();

    const OperationResult* halted = finish(session->halt("app"));
    ASSERT_NE(halted, nullptr);
    ASSERT_TRUE(halted->status) << halted->status.error().detail;
    ASSERT_TRUE(halted->targetStatus);
    EXPECT_TRUE(halted->targetStatus.value_or(hio::device::TargetStatus{}).halted);
    EXPECT_TRUE(target().get([](ScriptedTarget& t) { return t.halted; }));
    target().output(0, "while halted\n");
    EXPECT_TRUE(receives("while halted"));
    EXPECT_EQ(session->connection().state, ConnectionState::Connected);

    const OperationResult* status = finish(session->targetStatus("app"));
    ASSERT_NE(status, nullptr);
    ASSERT_TRUE(status->status);
    const auto found = status->targetStatus.value_or(hio::device::TargetStatus{});
    EXPECT_TRUE(found.halted);
    EXPECT_EQ(found.coreId, 0x2BA01477U);
    EXPECT_EQ(found.deviceName, "NRF52840_xxAA");

    const OperationResult* running = finish(session->go("app"));
    ASSERT_NE(running, nullptr);
    ASSERT_TRUE(running->status);
    EXPECT_FALSE(
        running->targetStatus
            .value_or(hio::device::TargetStatus{.halted = true, .coreId = 0, .deviceName = {}})
            .halted);
    target().output(0, "after go\n");
    EXPECT_TRUE(receives("after go"));

    EXPECT_EQ(rttStarts(), starts);                      // no re-attach
    EXPECT_EQ(rttStops(), stops);                        // no pause
    EXPECT_EQ(events().connections.size(), connections); // no state change at all
    EXPECT_EQ(events().count(ConnectionState::Disconnected), 0);
}

// ADR-0157: memory reads / writes, registers and zones on the console target keep RTT.
TEST_F(TargetOperations, MemoryRegistersAndZonesKeepRttAttached) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    const int starts = rttStarts();

    const OperationResult* written = finish(session->writeMemory(
        "app", 0x20000000, bytesOf({0x11, 0x22, 0x33, 0x44}), AccessWidth::Bits32));
    ASSERT_NE(written, nullptr);
    ASSERT_TRUE(written->status) << written->status.error().detail;
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.app.ram.at(3); }), 0x44);
    const auto sequence = target().get([](ScriptedTarget& t) { return t.ops; });
    EXPECT_NE(std::ranges::find(sequence, "writeU32"), sequence.end());

    const OperationResult* read16 =
        finish(session->readMemory("app", 0x20000000, 4, AccessWidth::Bits16));
    ASSERT_NE(read16, nullptr);
    ASSERT_TRUE(read16->status);
    EXPECT_EQ(read16->data, bytesOf({0x11, 0x22, 0x33, 0x44}));
    const OperationResult* read32 =
        finish(session->readMemory("app", 0x20000000, 4, AccessWidth::Bits32));
    ASSERT_NE(read32, nullptr);
    EXPECT_EQ(read32->data, bytesOf({0x11, 0x22, 0x33, 0x44})); // little-endian units

    target().output(0, "between operations\n");
    EXPECT_TRUE(receives("between operations"));

    // Registers need a halted core (target.not_halted from the worker).
    const OperationResult* running = finish(session->readRegisters("app"));
    ASSERT_NE(running, nullptr);
    ASSERT_FALSE(running->status);
    EXPECT_EQ(running->status.error().code, ErrorCode::TargetNotHalted);
    ASSERT_TRUE(finish(session->halt("app"))->status);
    const OperationResult* registers = finish(session->readRegisters("app"));
    ASSERT_NE(registers, nullptr);
    ASSERT_TRUE(registers->status);
    ASSERT_EQ(registers->registers.size(), 2U);
    EXPECT_EQ(registers->registers[1].name, "PC");
    EXPECT_EQ(registers->registers[1].value, 0x1000U);

    const OperationResult* zones = finish(session->memoryZones("app"));
    ASSERT_NE(zones, nullptr);
    ASSERT_TRUE(zones->status);
    ASSERT_EQ(zones->zones.size(), 1U);
    EXPECT_EQ(zones->zones[0].name, "AHB-AP");

    EXPECT_EQ(rttStarts(), starts);
    EXPECT_EQ(session->connection().state, ConnectionState::Connected);
    EXPECT_EQ(events().count(ConnectionState::Disconnected), 0);
}

// ADR-0157: on another target the light operations switch and restore (M2 pattern).
TEST_F(TargetOperations, LightOperationOnAnotherTargetSwitchesAndRestores) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    const int starts = rttStarts();
    const auto id = session->targetStatus("lte");
    const OperationResult* result = finish(id);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->status) << result->status.error().detail;
    EXPECT_EQ(result->targetStatus.value_or(hio::device::TargetStatus{}).deviceName,
              "NRF9151_XXCA");
    const auto planned = hio::device::plannedPhases(OperationKind::TargetStatus, true);
    EXPECT_EQ(events().phases(*id), std::vector<ProgramPhase>(planned.begin(), planned.end()));
    EXPECT_TRUE(waitFor([&] { return rttStarts() > starts; })); // re-attached on app
    EXPECT_TRUE(events().reached(ConnectionState::Connected));
}

// ADR-0099, ADR-0160: ranges are checked at request time (half-open); size and alignment too.
TEST_F(TargetOperations, AddressRangesAreCheckedAtRequestTime) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    const auto four = bytesOf({1, 2, 3, 4});
    // The immediate error code; nullopt when the request was accepted.
    const auto code = [](const hio::Result<OperationId>& id) -> std::optional<ErrorCode> {
        if (id) {
            return std::nullopt;
        }
        return id.error().code;
    };
    // Writes: RAM [0x20000000, 0x20040000), peripherals [0x40000000, 0x60000000), PPB.
    EXPECT_EQ(code(session->writeMemory("app", 0x1000, four, AccessWidth::Bits8)),
              std::optional(ErrorCode::TargetAddressOutOfRange)); // flash is not writable this way
    const auto outside = session->writeMemory("app", 0x2003FFFE, four, AccessWidth::Bits8);
    ASSERT_FALSE(outside);
    EXPECT_EQ(outside.error().code, ErrorCode::TargetAddressOutOfRange);
    EXPECT_NE(outside.error().detail.find("0x2003FFFE-0x20040002"), std::string::npos);
    EXPECT_NE(outside.error().detail.find("0x20000000-0x20040000"), std::string::npos);
    EXPECT_EQ(code(session->writeMemory("app", 0x60000000, four, AccessWidth::Bits8)),
              std::optional(ErrorCode::TargetAddressOutOfRange));
    EXPECT_EQ(code(session->writeMemory("app", 0x20000001, four, AccessWidth::Bits32)),
              std::optional(ErrorCode::UsageInvalidArgument)); // unaligned
    EXPECT_EQ(
        code(session->writeMemory("app", 0x20000000, bytesOf({1, 2, 3}), AccessWidth::Bits16)),
        std::optional(ErrorCode::UsageInvalidArgument)); // not a multiple of the width
    EXPECT_EQ(code(session->writeMemory("app", 0x20000000, {}, AccessWidth::Bits8)),
              std::optional(ErrorCode::UsageInvalidArgument));
    // Reads (ADR-0160): any range that does not wrap past 0xFFFFFFFF.
    EXPECT_EQ(code(session->readMemory("app", 0xFFFFFFFC, 8)),
              std::optional(ErrorCode::TargetAddressOutOfRange));
    EXPECT_EQ(code(session->readMemory("app", 0x20000000, 6, AccessWidth::Bits32)),
              std::optional(ErrorCode::UsageInvalidArgument));
    // The last word of RAM is inside (half-open end).
    EXPECT_TRUE(finish(session->writeMemory("app", 0x2003FFFC, four, AccessWidth::Bits8)));
}

// ADR-0160 (review R9): writes to the flash controller (NVMC) are rejected at request time,
// whatever the width; the detail names the denied range. nRF52840 NVMC at 0x4001E000: CONFIG
// 0x504, ERASEPAGE 0x508, ERASEALL 0x50C, ERASEUICR 0x514; nRF9160 NVMC at 0x50039000 (secure)
// and 0x40039000 (non-secure): CONFIG 0x504, ERASEALL 0x50C, ERASEPAGEPARTIALCFG 0x51C.
TEST_F(TargetOperations, FlashControllerWritesAreRejected) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    const auto erase = bytesOf({1, 0, 0, 0});
    const auto rejected = [&](std::string_view target, std::uint32_t address,
                              std::vector<std::byte> data, AccessWidth width) {
        const auto id = session->writeMemory(target, address, std::move(data), width);
        return !id && id.error().code == ErrorCode::TargetAddressOutOfRange;
    };
    for (const std::uint32_t offset : {0x504U, 0x508U, 0x50CU, 0x514U}) {
        for (const AccessWidth width :
             {AccessWidth::Bits8, AccessWidth::Bits16, AccessWidth::Bits32}) {
            EXPECT_TRUE(rejected("app", 0x4001E000U + offset, erase, width)) << offset;
        }
    }
    for (const std::uint32_t base : {0x50039000U, 0x40039000U}) {
        for (const std::uint32_t offset : {0x504U, 0x50CU, 0x51CU}) {
            EXPECT_TRUE(rejected("lte", base + offset, erase, AccessWidth::Bits32))
                << base + offset;
        }
        // Partial overlaps at both ends.
        EXPECT_TRUE(rejected("lte", base - 4, std::vector<std::byte>(8), AccessWidth::Bits32));
        EXPECT_TRUE(rejected("lte", base + 0xFFC, std::vector<std::byte>(8), AccessWidth::Bits32));
    }
    EXPECT_TRUE(rejected("app", 0x4001DFFE, std::vector<std::byte>(4), AccessWidth::Bits8));
    EXPECT_TRUE(rejected("app", 0x4001EFFF, std::vector<std::byte>(2), AccessWidth::Bits8));
    const auto detail = session->writeMemory("app", 0x4001E50C, erase, AccessWidth::Bits32);
    ASSERT_FALSE(detail);
    EXPECT_NE(detail.error().detail.find("denied 0x4001E000-0x4001F000"), std::string::npos)
        << detail.error().detail;
    EXPECT_FALSE(session->runningOperation()); // nothing reached the worker
    const auto sequence = target().get([](ScriptedTarget& t) { return t.ops; });
    EXPECT_EQ(std::ranges::count(sequence, "writeU32") + std::ranges::count(sequence, "writeMem"),
              0);
}

// ADR-0160 (review S1): reads are no longer restricted at request time; an unmapped address
// fails in the backend with target.memory_access_failed.
TEST_F(TargetOperations, UnmappedReadFailsInTheBackend) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    const OperationResult* result = finish(session->readMemory("app", 0x30000000, 4));
    ASSERT_NE(result, nullptr);
    ASSERT_FALSE(result->status);
    EXPECT_EQ(result->status.error().code, ErrorCode::TargetMemoryAccessFailed);
    EXPECT_EQ(session->connection().state, ConnectionState::Connected);
}

TEST_F(TargetOperations, WriteFlashProgramsVerifiesAndReattaches) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    const int starts = rttStarts();
    // Request-time errors (M2 rules, plan 0013).
    EXPECT_EQ(session->writeFlash("app", 0x10001080, bytesOf({1}), true).error().code,
              ErrorCode::FirmwareOutOfRange); // the PIB
    EXPECT_EQ(session->writeFlash("app", 0x10001000, bytesOf({1}), false).error().code,
              ErrorCode::FirmwareUicrInImage);
    EXPECT_EQ(session->writeFlash("app", 0x000FFFFF, bytesOf({1, 2}), false).error().code,
              ErrorCode::FirmwareOutOfRange);
    EXPECT_EQ(session->writeFlash("app", 0, std::vector<std::byte>((1024 * 1024) + 1), false)
                  .error()
                  .code,
              std::optional(ErrorCode::UsageInvalidArgument));

    target().set([](ScriptedTarget& t) { t.endDownloadDelay = std::chrono::milliseconds(200); });
    const auto id = session->writeFlash("app", 0x80000, bytesOf({0xAB, 0xCD}), false);
    ASSERT_TRUE(id);
    EXPECT_EQ(session->runningOperationKind(), OperationKind::WriteFlash);
    const auto busy = session->halt("app");
    ASSERT_FALSE(busy);
    EXPECT_EQ(busy.error().code, ErrorCode::InternalBusy);
    const OperationResult* result = finish(id);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->status) << result->status.error().detail;
    EXPECT_EQ(result->kind, OperationKind::WriteFlash);
    EXPECT_FALSE(session->runningOperationKind());
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.app.flash.at(0x80001); }), 0xCD);
    const auto planned = hio::device::plannedPhases(OperationKind::WriteFlash, false);
    EXPECT_EQ(events().phases(*id), std::vector<ProgramPhase>(planned.begin(), planned.end()));
    const auto sequence = target().get([](ScriptedTarget& t) { return t.ops; });
    EXPECT_EQ(sequence.back(), "go");                           // reset (go) last: the CPU runs
    EXPECT_TRUE(waitFor([&] { return rttStarts() > starts; })); // RTT re-attached
    EXPECT_TRUE(events().reached(ConnectionState::Connected));
}

// ADR-0151: a missing optional function fails only its operation.
TEST_F(TargetOperations, MissingOptionalFunction) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    target().set([](ScriptedTarget& t) {
        t.missing = {hio::jlink::OptionalFunction::IsHalted,
                     hio::jlink::OptionalFunction::WriteU16};
    });
    const OperationResult* status = finish(session->targetStatus("app"));
    ASSERT_NE(status, nullptr);
    ASSERT_FALSE(status->status);
    EXPECT_EQ(status->status.error().code, ErrorCode::DependencyJLinkFunctionMissing);
    EXPECT_EQ(status->status.error().detail, "JLINKARM_IsHalted");
    const OperationResult* write16 =
        finish(session->writeMemory("app", 0x20000000, bytesOf({1, 2}), AccessWidth::Bits16));
    ASSERT_NE(write16, nullptr);
    EXPECT_EQ(write16->status.error().detail, "JLINKARM_WriteU16");
    EXPECT_TRUE(finish(session->halt("app"))->status); // mandatory functions still work
    EXPECT_EQ(session->connection().state, ConnectionState::Connected);

    // Review S6: without JLINKARM_GetId only the core id is missing.
    target().set([](ScriptedTarget& t) { t.missing = {hio::jlink::OptionalFunction::GetId}; });
    const OperationResult* withoutId = finish(session->targetStatus("app"));
    ASSERT_NE(withoutId, nullptr);
    ASSERT_TRUE(withoutId->status) << withoutId->status.error().detail;
    ASSERT_TRUE(withoutId->targetStatus);
    const auto found = withoutId->targetStatus.value_or(hio::device::TargetStatus{});
    EXPECT_TRUE(found.halted);
    EXPECT_FALSE(found.coreId.has_value());
    EXPECT_EQ(found.deviceName, "NRF52840_xxAA");
}

// Review S7: halt on the console target stops auto-reconnect like reset --halt while the console
// stays Connected (ADR-0157); go lets it reconnect again.
TEST_F(TargetOperations, HaltStopsAutoReconnectUntilGo) {
    SessionOptions opts = options();
    opts.autoReconnect = true;
    auto session = openConnected(opts);
    ASSERT_TRUE(session);
    ASSERT_TRUE(finish(session->halt("app"))->status);
    EXPECT_EQ(session->connection().state, ConnectionState::Connected);
    const auto connects = [this] {
        return target().get([](ScriptedTarget& t) { return t.connects; });
    };
    target().set([](ScriptedTarget& t) { t.readFails = true; });
    ASSERT_TRUE(waitFor([&] { return events().reached(ConnectionState::Disconnected); }));
    const int before = connects();
    hio::device::test::spin(std::chrono::milliseconds(300)); // 5 reconnect intervals
    EXPECT_EQ(connects(), before);
    EXPECT_EQ(events().state(), std::optional(ConnectionState::Disconnected));

    ASSERT_TRUE(finish(session->go("app"))->status);
    EXPECT_TRUE(waitFor([&] { return connects() > before; })); // auto-reconnect runs again
    target().set([](ScriptedTarget& t) { t.readFails = false; });
    EXPECT_TRUE(waitFor([&] { return events().reached(ConnectionState::Connected); }));
}

// Review R20: a light operation on a Stopped console session (probe closed after a fatal first
// connect) runs and closes the probe again; the session stays Stopped and does not re-attach.
TEST_F(TargetOperations, LightOperationOnAStoppedSessionKeepsTheProbeClosed) {
    target().set([](ScriptedTarget& t) { t.openError = "Cannot open the USB device"; });
    SessionOptions opts = options();
    opts.autoReconnect = true;
    auto session = openIn(opts, ConnectionState::Stopped); // probe errors are fatal at first
    ASSERT_TRUE(session);
    target().set([](ScriptedTarget& t) { t.openError.reset(); });
    const std::size_t connections = events().connections.size();
    const OperationResult* status = finish(session->targetStatus("app"));
    ASSERT_NE(status, nullptr);
    ASSERT_TRUE(status->status) << status->status.error().detail;
    EXPECT_FALSE(target().get([](ScriptedTarget& t) { return t.open; }));
    hio::device::test::spin(std::chrono::milliseconds(200));
    EXPECT_EQ(events().connections.size(), connections); // still Stopped, nothing re-attached
    EXPECT_EQ(session->connection().state, ConnectionState::Stopped);
    EXPECT_FALSE(target().get([](ScriptedTarget& t) { return t.open; }));
    EXPECT_EQ(rttStarts(), 0);
}

// Review R20: after a failed reconnect (Disconnected, probe closed) a light operation leaves the
// probe closed and the state Disconnected; a probe that was open stays open.
TEST_F(TargetOperations, LightOperationOnADisconnectedSessionRestoresTheProbe) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    target().set([](ScriptedTarget& t) { t.openError = "Cannot open the USB device"; });
    ASSERT_TRUE(session->reconnect());
    ASSERT_TRUE(waitFor([&] {
        return events().reached(ConnectionState::Disconnected) &&
               events().connections.back().error.has_value();
    }));
    EXPECT_FALSE(target().get([](ScriptedTarget& t) { return t.open; }));
    target().set([](ScriptedTarget& t) { t.openError.reset(); });
    const std::size_t connections = events().connections.size();
    const OperationResult* halted = finish(session->halt("app"));
    ASSERT_NE(halted, nullptr);
    ASSERT_TRUE(halted->status) << halted->status.error().detail;
    EXPECT_TRUE(target().get([](ScriptedTarget& t) { return t.halted; }));
    EXPECT_FALSE(target().get([](ScriptedTarget& t) { return t.open; })); // closed again
    EXPECT_EQ(events().connections.size(), connections);
    EXPECT_EQ(session->connection().state, ConnectionState::Disconnected);

    // RTT lost but the probe open: it stays open after the operation.
    ASSERT_TRUE(session->reconnect());
    ASSERT_TRUE(waitFor([&] { return events().reached(ConnectionState::Connected); }));
    target().set([](ScriptedTarget& t) { t.readFails = true; });
    ASSERT_TRUE(waitFor([&] { return events().reached(ConnectionState::Disconnected); }));
    ASSERT_TRUE(finish(session->go("app"))->status);
    EXPECT_TRUE(target().get([](ScriptedTarget& t) { return t.open; }));
    EXPECT_EQ(session->connection().state, ConnectionState::Disconnected);
}

// releaseProbe keeps the ProbeLock and the registry slot; reacquireProbe connects again.
TEST_F(TargetOperations, ReleaseProbeKeepsTheLock) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    ASSERT_TRUE(session->releaseProbe());
    EXPECT_TRUE(waitFor([&] { return events().reached(ConnectionState::Stopped); }));
    EXPECT_FALSE(events().connections.back().error.has_value());
    EXPECT_FALSE(target().get([](ScriptedTarget& t) { return t.open; }));
    EXPECT_TRUE(hio::device::ProbeLock::owner(probeSn, root() / "locks").has_value());
    const hio::Status slot = registry().checkAvailable(std::optional<std::uint32_t>(probeSn));
    ASSERT_FALSE(slot); // still this session's registry slot
    EXPECT_EQ(slot.error().code, ErrorCode::ProbeOpenFailed);
    // Released: operations report the session as not connected.
    const OperationResult* halted = finish(session->halt("app"));
    ASSERT_NE(halted, nullptr);
    EXPECT_EQ(halted->status.error().code, ErrorCode::TargetNotConnected);

    const int starts = rttStarts();
    ASSERT_TRUE(session->reacquireProbe());
    EXPECT_TRUE(waitFor([&] { return events().reached(ConnectionState::Connected); }));
    EXPECT_TRUE(target().get([](ScriptedTarget& t) { return t.open; }));
    EXPECT_GT(rttStarts(), starts);
    target().output(0, "back again\n");
    EXPECT_TRUE(receives("back again"));
}

TEST_F(TargetOperations, ReacquireWhenTakenByAnotherProgram) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    ASSERT_TRUE(session->releaseProbe());
    EXPECT_TRUE(waitFor([&] { return events().reached(ConnectionState::Stopped); }));
    target().set([](ScriptedTarget& t) { t.openError = "J-Link is already in use"; });
    ASSERT_TRUE(session->reacquireProbe());
    EXPECT_TRUE(waitFor([&] {
        return events().reached(ConnectionState::Stopped) &&
               events().connections.back().error.has_value();
    }));
    const auto error = events().connections.back().error.value_or(hio::Error{});
    EXPECT_EQ(error.code, ErrorCode::ProbeInUseByOtherProcess);
    EXPECT_NE(error.detail.find("already in use"), std::string::npos);
    // No automatic retry while released; a later reacquire works.
    target().set([](ScriptedTarget& t) { t.openError.reset(); });
    ASSERT_TRUE(session->reacquireProbe());
    EXPECT_TRUE(waitFor([&] { return events().reached(ConnectionState::Connected); }));
}

// Review R19: reconnect() and resume() of a released session go through the reacquire path, so a
// probe taken by another program is probe.in_use_by_other_process (not probe.open_failed).
TEST_F(TargetOperations, ReconnectAndResumeOfAReleasedProbeReacquire) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    ASSERT_TRUE(session->releaseProbe());
    EXPECT_TRUE(waitFor([&] { return events().reached(ConnectionState::Stopped); }));
    target().set([](ScriptedTarget& t) { t.openError = "J-Link is already in use"; });
    const auto lastError = [this] {
        return events().connections.back().error.value_or(hio::Error{}).code;
    };
    ASSERT_TRUE(session->reconnect());
    EXPECT_TRUE(waitFor([&] {
        return events().reached(ConnectionState::Stopped) &&
               events().connections.back().error.has_value();
    }));
    EXPECT_EQ(lastError(), ErrorCode::ProbeInUseByOtherProcess);
    const std::size_t connections = events().connections.size();
    ASSERT_TRUE(session->resume());
    EXPECT_TRUE(waitFor([&] { return events().connections.size() > connections + 1; }));
    EXPECT_EQ(lastError(), ErrorCode::ProbeInUseByOtherProcess);
    EXPECT_EQ(events().state(), std::optional(ConnectionState::Stopped));
    // Still released: operations are not run.
    EXPECT_EQ(finish(session->halt("app"))->status.error().code, ErrorCode::TargetNotConnected);
    target().set([](ScriptedTarget& t) { t.openError.reset(); });
    ASSERT_TRUE(session->resume());
    EXPECT_TRUE(waitFor([&] { return events().reached(ConnectionState::Connected); }));
    target().output(0, "resumed\n");
    EXPECT_TRUE(receives("resumed"));
}

// Review R19: reacquireProbe() without a prior releaseProbe() is a no-op (no reconnect).
TEST_F(TargetOperations, ReacquireWithoutReleaseIsANoOp) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    const auto counters = [this] {
        return target().get([](ScriptedTarget& t) { return std::pair(t.opens, t.connects); });
    };
    const auto before = counters();
    const int starts = rttStarts();
    const std::size_t connections = events().connections.size();
    ASSERT_TRUE(session->reacquireProbe());
    hio::device::test::spin(std::chrono::milliseconds(200));
    EXPECT_EQ(counters(), before);
    EXPECT_EQ(rttStarts(), starts);
    EXPECT_EQ(events().connections.size(), connections);
    EXPECT_EQ(session->connection().state, ConnectionState::Connected);
    // A release after it still works as usual.
    ASSERT_TRUE(session->releaseProbe());
    EXPECT_TRUE(waitFor([&] { return events().reached(ConnectionState::Stopped); }));
    ASSERT_TRUE(session->reacquireProbe());
    EXPECT_TRUE(waitFor([&] { return events().reached(ConnectionState::Connected); }));
}

// The RTT control block address reaches RTT start (sticky; nullopt = search).
TEST_F(TargetOperations, RttAddressIsPassedAndSticky) {
    SessionOptions opts = options();
    opts.rttAddress = 0x20001000U;
    auto session = openConnected(opts);
    ASSERT_TRUE(session);
    const auto addresses = [this] {
        return target().get([](ScriptedTarget& t) { return t.rttStartAddresses; });
    };
    ASSERT_FALSE(addresses().empty());
    EXPECT_TRUE(std::ranges::all_of(addresses(), [](const std::optional<std::uint32_t>& a) {
        return a == std::optional<std::uint32_t>(0x20001000U);
    }));
    ASSERT_TRUE(session->setRttAddress(0x20002000U));
    ASSERT_TRUE(session->reconnect());
    EXPECT_TRUE(waitFor([&] {
        return addresses().back() == std::optional<std::uint32_t>(0x20002000U) &&
               events().reached(ConnectionState::Connected);
    }));
    ASSERT_TRUE(session->setRttAddress(std::nullopt));
    const std::size_t before = addresses().size();
    ASSERT_TRUE(session->reconnect());
    EXPECT_TRUE(waitFor([&] {
        const auto all = addresses();
        return all.size() > before && !all.back().has_value();
    }));
}

// ADR-0104: the RTT prompt of the profile is reported (not as a line), never without one.
TEST_F(TargetOperations, PromptSeenWithAProfilePrompt) {
    SessionOptions opts = options();
    opts.profile = &promptProfile;
    auto session = openConnected(opts);
    ASSERT_TRUE(session);
    target().output(0, "Firmware version: v3\r\n\x1b[1;32muart:~$ \x1b[m");
    EXPECT_TRUE(waitFor([&] { return !events().prompts.empty(); }));
    EXPECT_TRUE(receives("Firmware version: v3"));
    EXPECT_EQ(events().prompts.size(), 1U);
    EXPECT_EQ(events().prompts.front(), events().lines.size()); // after the line
    target().output(0, "next\n");
    EXPECT_TRUE(receives("next"));
    const auto lines = events().texts(Channel::Shell, Direction::Out);
    EXPECT_EQ(std::ranges::count_if(lines,
                                    [](const std::string& line) {
                                        return line.find("uart:~$") != std::string::npos;
                                    }),
              0);
    EXPECT_EQ(events().prompts.size(), 1U);
}

TEST_F(TargetOperations, NoPromptSeenWithoutAProfilePrompt) {
    auto session = openConnected(options()); // CHESTER: shellPrompt empty
    ASSERT_TRUE(session);
    target().output(0, "x\nuart:~$ ");
    EXPECT_TRUE(receives("x"));
    hio::device::test::spin(std::chrono::milliseconds(100));
    EXPECT_TRUE(events().prompts.empty());
}

// ADR-0156: the default console file goes into consoleFileDir; an explicit file wins.
TEST_F(TargetOperations, ConsoleFileDir) {
    const fs::path dir = root() / "mcp-console";
    fs::create_directories(dir);
    SessionOptions opts = options();
    opts.consoleFile = std::nullopt;
    opts.consoleFileDir = dir;
    {
        auto session = openConnected(opts);
        ASSERT_TRUE(session);
        EXPECT_TRUE(waitFor([&] { return !session->consoleFile().empty(); }));
        EXPECT_EQ(session->consoleFile().parent_path(), dir);
        EXPECT_TRUE(session->consoleFile().filename().string().starts_with("chester-app-"));
        session->close();
    }
    opts.consoleFile = root() / "explicit.log";
    auto session = openConnected(opts);
    ASSERT_TRUE(session);
    EXPECT_TRUE(waitFor([&] { return !session->consoleFile().empty(); }));
    EXPECT_EQ(session->consoleFile(), root() / "explicit.log");
}

} // namespace
