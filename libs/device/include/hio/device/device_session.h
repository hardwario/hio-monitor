#pragma once

// DeviceSession - the single owner of one probe connection (ADR-0031, ADR-0034, ADR-0053, ADR-0058,
// ADR-0063; plan/features/0002-m1-jlink-rtt.md).
//
// A QObject facade on the owner thread (GUI main thread or the CLI's QCoreApplication thread) and
// one owned std::jthread worker. The worker owns the J-Link library copy (ADR-0030), the probe, the
// RTT channel, the line assemblers and the console file; every vendor call of the session happens
// there. Commands go to the worker through a BoundedQueue (capacity commandQueueCapacity, Reject ->
// internal.queue_full); events come back through a BoundedQueue (eventQueueCapacity; when full the
// oldest console lines are dropped, never other events, losses reported as linesDropped). The
// worker wakes the facade with one coalesced queued call (atomic flag); the facade drains the
// events, appends the lines to the ConsoleLog and emits batched signals.
//
// open() is non-blocking (ADR-0053): it validates the options, takes the registry slot and (when a
// serial number is given) the cross-process ProbeLock (ADR-0058), starts the worker and returns.
// The worker then loads the library, opens the probe (without a serial number: the first probe
// that is not locked, then locks it), connects the target and attaches RTT, reporting Connecting
// -> Connected, or Disconnected (auto-reconnect on) / Stopped (fatal: library, lock or probe
// errors, or any error with auto-reconnect off) with the hio::Error. Every step checks the stop
// token, so close() returns within one step (apart from a single blocking vendor call).
//
// Worker behaviour (ADR-0034): the command is echoed only after it was written; a read failure
// reports Disconnected and reading continues; only received data or a successful write reports
// Connected again; the target voltage is checked every second while idle; auto-reconnect every
// 3 s, only while the target is powered; an explicit reconnect is always tried; a suspended
// session never re-attaches automatically; connection events are reported only on a change. An
// outdated or newer probe firmware is reported once as a warning (probe.firmware_outdated), never
// updated automatically.
//
// Operations (M2, plan/features/0003, ADR-0057, ADR-0062): flash, flashLteBundle, erase, recover,
// updateModem, reset and readMemory each queue one command for the worker and return its
// OperationId at once; progress and the one result arrive as operationProgress /
// operationFinished. One operation at a time (internal.busy). Every operation names its target,
// a target of the session's profile (usage.invalid_argument otherwise). The worker pauses RTT
// (ADR-0034 "stop without reporting a disconnect"); for another target than the console target it
// moves the probe there (SwitchingTarget) and back (Restoring), also after a failure or cancel;
// a failed restore reports Disconnected with the error. Afterwards RTT is re-attached unless the
// CPU was left halted (flash --halt, reset halt, erase). Modem updates release the probe for
// nRF Util and open it again afterwards, also after a failure or cancel. Operations also run on
// sessions opened with attachRtt = false. cancel() ends the operation with `cancelled` at its next
// stop check (never inside an erase-all, a recover or one flash-loader download).
//
// M6 (plan/features/0013, ADR-0099, ADR-0157): halt, go, targetStatus, readMemory with a width,
// writeMemory, readRegisters, memoryZones and writeFlash are operations too; the light ones keep
// RTT attached on the console target. releaseProbe / reacquireProbe and a sticky RTT control
// block address; the RTT shell prompt of the profile is reported as promptSeen (ADR-0104).
//
// M5 (ADR-0079, plan/features/0011): DeviceSession implements the transport-neutral ConsoleSession
// (identity {JLink, "<sn>", "J-Link <sn>"}, all capabilities, consoleTargetId() = its target);
// the J-Link operations, suspend/resume, probe(), diagnose and startConnection stay here.
//
// Thread safety: every member function must be called on the owner thread.

#include "hio/core/bounded_queue.h"
#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/console_session.h" // IWYU pragma: export (waitUntilConnected, M1 users)
#include "hio/device/operation.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/lte_input.h"
#include "hio/hw/jlink/jlink_probe.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/rtt_channel.h"
#include "hio/hw/jlink/types.h"
#include "hio/hw/nrfutil/nrfutil.h"

#include <QList>
#include <QObject>
#include <QtTypes>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace hio::device {

class SessionRegistry;

namespace detail {
struct SessionInternals;
struct OperationCommand;
} // namespace detail

struct SessionOptions {
    const DeviceProfile* profile = nullptr;
    std::string_view targetId;
    std::optional<std::uint32_t> probeSn; // nullopt = the first probe that is not locked
    // J-Link speed: nullopt = automatic (the default, as pylink; ADR-0090), otherwise this fixed
    // speed in kHz (minSpeedKhz..maxSpeedKhz) for the connect and the session.
    std::optional<int> speedKhz;
    int latencyMs = 0; // RTT poll interval (minLatencyMs..maxLatencyMs)
    bool autoReconnect = false;
    bool resetOnOpen = false; // reset the target (no halt) before attaching
    // false: the session connects the target but never attaches RTT (flash-only sessions; the CLI
    // flash commands). A target without an RTT console needs attachRtt = false.
    bool attachRtt = true;
    // M4 (ADR-0069): false = a probe-only session: the worker loads the library, opens the probe,
    // takes its ProbeLock and reports ProbeOpen; no target connect, no RTT, no power polling, no
    // auto-reconnect. Only operations (Diagnose, flash, ...) touch the target, and afterwards the
    // session stays ProbeOpen. startConnection() promotes it to a normal session.
    bool connectTarget = true;
    // nRF Util for updateModem / flashLteBundle (ADR-0016), located on the worker when a modem
    // update starts. nullopt (default) = nrfutil::defaultLocatorOptions() with the user's settings
    // (HIO_NRFUTIL, nrfutil/path, PATH, install directories; review R9); the CLI passes its
    // --nrfutil there. This snapshot is taken in open() and used when nrfUtilProvider is empty.
    std::optional<nrfutil::LocatorOptions> nrfUtil;
    // ADR-0114: called on the owner thread by updateModem() / flashLteBundle() each time such an
    // operation is requested; the options it returns travel with that operation to the worker,
    // so a path changed after open() (GUI Settings, setup wizard) is used. Must not throw. Empty
    // (default) = the `nrfUtil` snapshot.
    std::function<nrfutil::LocatorOptions()> nrfUtilProvider;
    // nullopt = default console file, empty path = no console file, otherwise this file.
    std::optional<std::filesystem::path> consoleFile;
    // ADR-0063: nullptr = the session creates its own log; a GUI tab passes one log for all its
    // sessions. Accessed on the owner thread only.
    std::shared_ptr<ConsoleLog> consoleLog;
    // ADR-0156 (M6): the default console file goes into this directory (same generated name);
    // an explicit consoleFile (or an empty path = none) wins. nullopt = the default directory.
    std::optional<std::filesystem::path> consoleFileDir = std::nullopt;
    // M6 (plan/features/0013): the RTT control block address passed to RTT start (rttt's
    // --address); nullopt = the library searches for it. setRttAddress() changes it (sticky).
    std::optional<std::uint32_t> rttAddress = std::nullopt;
};

// Option ranges checked by open() (usage.invalid_argument). The speed range is pylink's
// (MIN_JTAG_SPEED, MAX_JTAG_SPEED); it applies to a fixed speed only.
inline constexpr int minSpeedKhz = 5;
inline constexpr int maxSpeedKhz = 50000;
inline constexpr int minLatencyMs = 1;
inline constexpr int maxLatencyMs = 1000;

class DeviceSession final : public ConsoleSession {
    Q_OBJECT

    struct Passkey {
        explicit Passkey() = default;
    };

public:
    static constexpr std::size_t commandQueueCapacity = 256;
    static constexpr std::size_t eventQueueCapacity = 10000;

    // Non-blocking (ADR-0053): validates options, takes the registry slot (and the probe lock when
    // probeSn is given), starts the worker, returns at once. Errors: usage.invalid_argument,
    // internal.session_limit, probe.open_failed (SN already used by this application),
    // probe.in_use_by_other_process, io.write_failed (lock directory).
    [[nodiscard]] static Result<std::unique_ptr<DeviceSession>>
    open(SessionRegistry& registry, jlink::LibraryLocation location, SessionOptions options);

    DeviceSession(Passkey key, SessionRegistry& registry,
                  std::unique_ptr<detail::SessionInternals> internals);
    ~DeviceSession() override; // closes (closeSession())
    DeviceSession(const DeviceSession&) = delete;
    DeviceSession& operator=(const DeviceSession&) = delete;
    DeviceSession(DeviceSession&&) = delete;
    DeviceSession& operator=(DeviceSession&&) = delete;

    // Queued for the worker; an Error only when the queue is full (internal.queue_full) or the
    // session is closed (cancelled). Results arrive as events (echo line, warning, connection).
    [[nodiscard]] Status sendLine(std::string line) override;
    [[nodiscard]] Status reconnect() override;
    [[nodiscard]] Status suspend(); // detach and release the probe (M2 / nRF Util)
    [[nodiscard]] Status resume();  // allow attaching again and re-attach now

    // ---- operations (M2, ADR-0062) ----
    // Errors (immediate): usage.invalid_argument (unknown target, empty image, bad size),
    // internal.busy, internal.queue_full, cancelled (session closed).
    [[nodiscard]] Result<OperationId> flash(std::string_view targetId,
                                            std::shared_ptr<const firmware::FirmwareImage> image,
                                            FlashOptions options);
    [[nodiscard]] Result<OperationId>
    flashLteBundle(std::string_view targetId, std::shared_ptr<const firmware::LteBundle> bundle,
                   FlashOptions options);
    [[nodiscard]] Result<OperationId> erase(std::string_view targetId, EraseScope scope);
    [[nodiscard]] Result<OperationId> recover(std::string_view targetId);
    [[nodiscard]] Result<OperationId> updateModem(std::string_view targetId,
                                                  std::filesystem::path modemZip);
    // Replaces M1's reset(bool halt) (ADR-0062).
    [[nodiscard]] Result<OperationId> reset(std::string_view targetId, ResetMode mode);
    // 1..maxReadMemoryBytes bytes; M6: `size` a multiple of the width, `address` aligned to it,
    // any range that does not run past 0xFFFFFFFF (ADR-0160; target.address_out_of_range); an
    // unmapped address fails in the worker with target.memory_access_failed. 16/32-bit units are
    // returned little-endian in `data`.
    [[nodiscard]] Result<OperationId> readMemory(std::string_view targetId, std::uint32_t address,
                                                 std::size_t size,
                                                 AccessWidth width = AccessWidth::Bits8);

    // ---- low-level target operations (M6, plan/features/0013, ADR-0099) ----
    // halt, go, targetStatus, readMemory, writeMemory, readRegisters and memoryZones are light
    // operations (ADR-0157): on the console target RTT stays attached and the connection state
    // does not change (a halted core keeps the console Connected); on another target they switch
    // and restore like the M2 operations. The worker checks the family first (review R7).
    // Review R20: on a console target whose RTT is not attached (Disconnected, Stopped,
    // ProbeOpen) the reported state does not change either, and a probe that was closed before
    // the operation is closed again after it.
    // Missing optional J-Link functions: dependency.jlink_function_missing (ADR-0151).
    // Result: targetStatus.halted = true / false. Review S7: halt on the console target stops
    // auto-reconnect like reset(Halt) (the console stays Connected); go, reset(Go), reconnect()
    // and resume() end that.
    [[nodiscard]] Result<OperationId> halt(std::string_view targetId);
    [[nodiscard]] Result<OperationId> go(std::string_view targetId);
    // Result: targetStatus {halted, coreId, deviceName}; coreId is nullopt when the library lacks
    // JLINKARM_GetId (review S6; JLINKARM_IsHalted is still required).
    [[nodiscard]] Result<OperationId> targetStatus(std::string_view targetId);
    // 1..maxWriteMemoryBytes bytes (units little-endian), a multiple of the width, aligned, inside
    // the target's RAM or one peripheral range and not overlapping TargetProfile::writeDenied (the
    // flash controller, ADR-0160; checked again by the worker). Errors (immediate):
    // usage.invalid_argument, target.address_out_of_range (detail: requested and allowed ranges,
    // or the denied range), and those above.
    [[nodiscard]] Result<OperationId> writeMemory(std::string_view targetId, std::uint32_t address,
                                                  std::vector<std::byte> data, AccessWidth width);
    // Result: registers (<= maxRegisters). Worker error: target.not_halted.
    [[nodiscard]] Result<OperationId> readRegisters(std::string_view targetId);
    // Result: zones (<= maxMemoryZones; empty when the library reports none).
    [[nodiscard]] Result<OperationId> memoryZones(std::string_view targetId);
    // Raw bytes into flash (1..maxWriteFlashBytes): reset + halt, program (the flash loader keeps
    // the untouched bytes of each sector), verify, reset (go), RTT re-attach (an M2-style
    // operation; phases of flash). Errors (immediate, planImage of M2): firmware.out_of_range
    // (outside flash and UICR, or touching the PIB), firmware.uicr_in_image (UICR without
    // `allowUicr`), usage.invalid_argument.
    [[nodiscard]] Result<OperationId> writeFlash(std::string_view targetId, std::uint32_t address,
                                                 std::vector<std::byte> data, bool allowUicr);
    // M4 (ADR-0069): the live connection check of `targetId`: probe, power, connect, identify,
    // rtt, rtt-buffers. Each step is reported by diagnoseStep() as it finishes; the result's
    // `checks` holds all steps. Never resets, halts or writes the target. On the console target of
    // an attached session the RTT steps check the running attachment; another target is switched
    // to and back (TargetSwitch). Total about diagnoseBudget (a blocking vendor call may exceed
    // it; the step is then a Warning diag.check_timeout).
    [[nodiscard]] Result<OperationId> diagnose(std::string_view targetId);
    // M4 (ADR-0069): promotes a probe-only session (state ProbeOpen) to a normal one: connects the
    // target, attaches RTT when `attachRtt`, then power polling and auto-reconnect per
    // SessionOptions. The probe and its ProbeLock stay held. A second call before the session has
    // left ProbeOpen is a no-op (success; the first call's `attachRtt` applies). Errors:
    // usage.invalid_argument (not ProbeOpen, or attachRtt on a target without a console),
    // internal.busy (an operation runs), internal.queue_full, cancelled (closed).
    [[nodiscard]] Status startConnection(bool attachRtt);
    // Errors: usage.invalid_argument when `id` is not the running operation.
    [[nodiscard]] Status cancel(OperationId id);
    // The running operation (nullopt when none).
    [[nodiscard]] std::optional<OperationId> runningOperation() const;
    // M6: the kind of the running operation (nullopt when none), e.g. for internal.busy details.
    [[nodiscard]] std::optional<OperationKind> runningOperationKind() const;

    // ---- probe release (M6, plan/features/0013; rttt jlink_close / jlink_open) ----
    // Queued like suspend(): stops RTT and closes the J-Link connection so that another program
    // (J-Link Commander, west flash) can use the probe; the session reports Stopped. The
    // ProbeLock and the registry slot stay held (the lock binds HARDWARIO programs only). While
    // released (review R19), reconnect() and resume() reacquire the probe like reacquireProbe().
    // Errors: internal.queue_full, cancelled (closed).
    [[nodiscard]] Status releaseProbe();
    // Queued: opens the J-Link again and connects (Connecting -> Connected / Disconnected). When
    // the probe cannot be opened (taken by the other program) the session reports Stopped with
    // probe.in_use_by_other_process. Without a prior releaseProbe() it is a no-op (review R19).
    // Errors (immediate): internal.queue_full, cancelled.
    [[nodiscard]] Status reacquireProbe();
    // Queued: the RTT control block address for every later attach (sticky; nullopt = search),
    // e.g. before reconnect(). Errors: internal.queue_full, cancelled.
    [[nodiscard]] Status setRttAddress(std::optional<std::uint32_t> address);
    // The result of `id` when it is the last finished operation - also after close(), which takes
    // a result the worker delivered after a waiter gave up (review R13).
    [[nodiscard]] std::optional<OperationResult> finishedResult(OperationId id) const;

    // close() (ConsoleSession) stops and joins the worker, releases the probe lock and the
    // registry slot. Idempotent.

    // Owner thread only; the provided or the session's own log.
    [[nodiscard]] const ConsoleLog& log() const override;
    // The probe; serialNumber is 0 until the worker has opened it (connectionChanged is emitted
    // after it is set).
    [[nodiscard]] jlink::ProbeInfo probe() const;
    // The console log file the session writes now (amended by plan 0005): empty before the worker
    // opened it (after the probe is identified), without a console file and after a write failure
    // stopped it (the session's io.write_failed warning). Updated by the worker's events, like
    // probe(); a continuation file (.partN.log) replaces the name. Owner thread only.
    [[nodiscard]] std::filesystem::path consoleFile() const override;
    // The last reported connection event (Connecting before the first one).
    [[nodiscard]] const ConnectionEvent& connection() const noexcept override {
        return m_connection;
    }
    // {JLink, "<sn>", "J-Link <sn>"}: the opened probe's serial number, before that the requested
    // one ("" when the first free probe is used and not opened yet).
    [[nodiscard]] SessionIdentity identity() const override;
    // Reset, LogChannel and Operations.
    [[nodiscard]] Capabilities capabilities() const override;
    [[nodiscard]] const DeviceProfile& profile() const noexcept override;
    [[nodiscard]] std::string_view consoleTargetId() const override;
    [[nodiscard]] const TargetProfile& target() const noexcept;

signals:
    void operationProgress(hio::device::OperationId id,
                           const hio::device::OperationProgress& progress);
    void operationFinished(hio::device::OperationId id, const hio::device::OperationResult& result);
    // M4: one per finished Diagnose step, in order, before operationFinished (queued; ADR-0069).
    void diagnoseStep(hio::device::OperationId id, const hio::device::DiagnoseStep& step);

protected:
    void closeSession() override;

private:
    friend struct detail::SessionInternals;

    // The close path of closeSession() and the destructor. Idempotent.
    void closeNow();

    // Owner thread: invoked (queued, by name) by wakeUp().
    Q_INVOKABLE void drainEvents();
    // A received line: to a silent shell query (plan 0021, ADR-0175) or into the log and `batch`.
    void receiveLine(ConsoleLine&& line, QList<ConsoleLine>& batch);

    [[nodiscard]] Result<OperationId> startOperation(detail::OperationCommand command,
                                                     std::string_view targetId);

    // Worker thread. Queues drainEvents() by name, so the queued call object is created and
    // consumed inside Qt (its own locking), not in a functor of ours that ThreadSanitizer would
    // see crossing threads without the uninstrumented Qt mutex.
    void wakeUp();

    SessionRegistry* m_registry;
    std::unique_ptr<detail::SessionInternals> m_internals;
    std::atomic<bool> m_wakePending{false};
    ConnectionEvent m_connection;
    jlink::ProbeInfo m_probe;
    std::filesystem::path m_consoleFile;
    std::uint64_t m_reportedDrops = 0;
    bool m_closed = false;
};

// CLI helper: runs a local QEventLoop until operation `id` finished and returns its result.
// `onProgress` gets every progress event. A stop request cancels the operation (DeviceSession::
// cancel) and keeps waiting for its result (`cancelled`, or what it ended with) for at most
// `cancelGrace` (one flash-loader download, erase-all or recover is not interrupted). The wait
// also ends when the session is closed or its worker dies (the result is then `cancelled` /
// `internal.unexpected`; review R4). The grace bounds only the wait: close() still joins the
// worker, which finishes its current step (it cannot be interrupted mid-step), and the real
// result is then available from finishedResult() (review R13). In the CLI a second Ctrl+C
// force-exits.
[[nodiscard]] OperationResult
waitForOperation(DeviceSession& session, OperationId id,
                 const std::function<void(const OperationProgress&)>& onProgress = {},
                 const std::stop_token& stop = {},
                 std::chrono::milliseconds cancelGrace = std::chrono::seconds(90));

// The parts of the worst-case first connect that are not timings of the session (review R38;
// plan/features/0002 Bounds).
struct ConnectTimeoutBudget {
    // Copy + load of the library, probe list and open.
    std::chrono::milliseconds libraryLoad{5000};
    // One JLINKARM_Connect call (the library has no timeout of ours; a target that does not
    // answer takes a few seconds).
    std::chrono::milliseconds connectAttempt{3000};
    // Power reading, optional reset, scheduling.
    std::chrono::milliseconds margin{2000};
};

// The timeout for waitUntilConnected() / waitForFirstAttempt() that covers the worst-case first
// attempt, so that its own error (e.g. target.rtt_not_found) arrives before `timeout`:
//   library load + connectAttempts x (connect attempt + retry delay)
//   + RTT attach deadline + logger wait (it may start just before the attach deadline) + margin.
[[nodiscard]] constexpr std::chrono::milliseconds
connectTimeoutFor(const jlink::ProbeTiming& probe, const jlink::RttTiming& rtt,
                  const ConnectTimeoutBudget& budget = {}) {
    return budget.libraryLoad +
           (probe.connectAttempts * (budget.connectAttempt + probe.connectRetryDelay)) +
           rtt.attachDeadline + rtt.loggerWait + budget.margin;
}

// The timeout the CLI passes to the waits above for a session at J-Link speed `speedKhz`
// (nullopt = auto), with the attach deadline of that speed (ADR-0110), as DeviceSession::open()
// uses it.
[[nodiscard]] constexpr std::chrono::milliseconds
connectTimeoutForSpeed(std::optional<int> speedKhz) {
    jlink::RttTiming rtt;
    rtt.attachDeadline = jlink::rttAttachDeadline(speedKhz, rtt.attachDeadline);
    return connectTimeoutFor(jlink::ProbeTiming{}, rtt);
}

// At auto speed and at 1000 kHz or more: 5 + 3 x (3 + 0.5) + 15 + 3 + 2 = 35.5 s; at 250 kHz and
// below (attach deadline capped at 60 s): 5 + 3 x (3 + 0.5) + 60 + 3 + 2 = 80.5 s.
inline constexpr std::chrono::milliseconds defaultConnectTimeout =
    connectTimeoutFor(jlink::ProbeTiming{}, jlink::RttTiming{});
static_assert(defaultConnectTimeout == std::chrono::milliseconds(35500));
static_assert(connectTimeoutForSpeed(std::nullopt) == defaultConnectTimeout);
static_assert(connectTimeoutForSpeed(minSpeedKhz) == std::chrono::milliseconds(80500));

// ADR-0110: the warning for a fixed J-Link speed below jlink::rttSlowSpeedKhz (translated), shown
// before connecting; nullopt at auto and at faster speeds.
[[nodiscard]] std::optional<std::string> slowSpeedWarning(std::optional<int> speedKhz);

} // namespace hio::device
