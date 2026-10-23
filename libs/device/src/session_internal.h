#pragma once

// Internals of DeviceSession shared by the facade (device_session.cpp), the worker
// (session_worker.cpp) and the unit tests (which inject an IJLinkApi factory and short timings).

#include "hio/core/bounded_queue.h"
#include "hio/core/result.h"
#include "hio/device/console_file_sink.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/device_session.h"
#include "hio/device/operation.h"
#include "hio/device/probe_lock.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/lte_input.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/jlink_probe.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/rtt_channel.h"
#include "hio/hw/jlink/types.h"
#include "hio/hw/nrfutil/nrfutil.h"

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
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace hio::device {

class SessionRegistry;

namespace detail {

// ---- commands (owner thread -> worker) --------------------------------------------------------

struct SendLineCommand {
    std::string line;
};
struct ReconnectCommand {};
struct SuspendCommand {};
struct ResumeCommand {};
// M4 (ADR-0069): promote a probe-only session.
struct StartConnectionCommand {
    bool attachRtt = true;
};
// M6 (plan/features/0013): DeviceSession::releaseProbe() / reacquireProbe() / setRttAddress().
struct DetachProbeCommand {};
struct ReattachProbeCommand {};
struct SetRttAddressCommand {
    std::optional<std::uint32_t> address;
};

// One session operation (plan/features/0003, ADR-0057, ADR-0062). The images are immutable and
// shared with the owner thread; `stop` is the operation's cancel token (DeviceSession::cancel).
struct OperationCommand {
    OperationId id = 0;
    OperationKind kind = OperationKind::Reset;
    const TargetProfile* target = nullptr;
    std::shared_ptr<const firmware::FirmwareImage> image; // Flash
    std::shared_ptr<const firmware::LteBundle> bundle;    // LteBundle
    std::filesystem::path modemZip;                       // Modem
    // Modem, LteBundle: the nRF Util locator options from SessionOptions::nrfUtilProvider at
    // request time (ADR-0114); nullopt = WorkerConfig::nrfUtil (the snapshot of open()).
    std::optional<nrfutil::LocatorOptions> nrfUtil;
    FlashOptions flash;
    ResetMode reset = ResetMode::Go;
    std::uint32_t address = 0;              // ReadMemory, WriteMemory
    std::size_t size = 0;                   // ReadMemory
    AccessWidth width = AccessWidth::Bits8; // ReadMemory, WriteMemory (M6)
    std::vector<std::byte> data;            // WriteMemory (M6; WriteFlash uses `image`)
    std::stop_token stop;
};

using Command = std::variant<SendLineCommand, ReconnectCommand, SuspendCommand, ResumeCommand,
                             OperationCommand, StartConnectionCommand, DetachProbeCommand,
                             ReattachProbeCommand, SetRttAddressCommand>;

// ---- events (worker -> owner thread) ----------------------------------------------------------

// The probe the worker opened; sets DeviceSession::probe() before the next connection event.
struct ProbeIdentified {
    jlink::ProbeInfo probe;
};

// The console file the worker writes now (opened, continued in a .partN file, or stopped after a
// write failure: empty); sets DeviceSession::consoleFile() (plan 0002, amended by 0005).
struct ConsoleFileChanged {
    std::filesystem::path file;
};

struct OperationProgressEvent {
    OperationId id = 0;
    OperationProgress progress;
};
struct OperationFinishedEvent {
    OperationId id = 0;
    OperationResult result;
};
struct DiagnoseStepEvent {
    OperationId id = 0;
    DiagnoseStep step;
};

// M6 (ADR-0104): hio::device::PromptSeen, the RTT shell prompt after output.
using WorkerEvent =
    std::variant<ConsoleLine, ConnectionEvent, Warning, ProbeIdentified, ConsoleFileChanged,
                 OperationProgressEvent, OperationFinishedEvent, DiagnoseStepEvent, PromptSeen>;

// The events the event queue may drop when it is full: shell and log output (Direction::Out).
// Echoes of written commands (Direction::In) are kept, so a caller that counts the written lines
// (`chester app command`) never misses one (review R54); they are bounded by the command queue.
[[nodiscard]] inline bool isDroppableEvent(const WorkerEvent& event) {
    const auto* line = std::get_if<ConsoleLine>(&event);
    return line != nullptr && line->dir == Direction::Out;
}

// ---- configuration ----------------------------------------------------------------------------

using ApiFactory = std::function<Result<std::unique_ptr<jlink::IJLinkApi>>()>;
// Locates (with the given options) and checks nRF Util and returns the modem updater (runs on the
// worker; ADR-0042, ADR-0114).
using ModemUpdaterFactory = std::function<Result<std::unique_ptr<nrfutil::IModemUpdater>>(
    const nrfutil::LocatorOptions& locator)>;

struct SessionTiming {
    std::chrono::milliseconds powerCheckInterval{1000}; // ADR-0034
    std::chrono::milliseconds reconnectInterval{3000};  // ADR-0034
    jlink::ProbeTiming probe;
    jlink::RttTiming rtt;
    jlink::ProgrammerTiming programmer;
    // Operation progress events: at most one per interval per operation (plus phase changes and
    // the final one).
    std::chrono::milliseconds progressInterval{100};
    // Diagnose (ADR-0069): the overall budget and the RTT search budget of the `rtt` step.
    std::chrono::milliseconds diagnoseBudget = device::diagnoseBudget;
    std::chrono::milliseconds diagnoseRttBudget = device::diagnoseRttBudget;
};

struct WorkerConfig {
    const DeviceProfile* profile = nullptr;
    const TargetProfile* target = nullptr;
    std::optional<std::uint32_t> probeSn;
    std::optional<int> speedKhz; // nullopt = automatic (ADR-0090)
    int latencyMs = 0;
    bool autoReconnect = false;
    bool resetOnOpen = false;
    bool attachRtt = true;
    bool connectTarget = true;                    // M4: false = probe-only session (ADR-0069)
    std::optional<ConsoleFileConfig> consoleFile; // nullopt = no console file
    // M6: SessionOptions::rttAddress; changed by SetRttAddressCommand (sticky).
    std::optional<std::uint32_t> rttAddress;
    std::filesystem::path lockDirectory;
    // The nRF Util locator snapshot of open() (SessionOptions::nrfUtil or the defaults), used by a
    // modem step whose command carries no options (ADR-0114).
    nrfutil::LocatorOptions nrfUtil;
    SessionTiming timing;
    // Test hook (review R40): incremented once per worker loop iteration, so a test can wait for
    // a number of cycles instead of a fixed time. nullptr = not counted (production).
    std::shared_ptr<std::atomic<std::uint64_t>> cycles;
    // M4 (ADR-0072): the trees checkUsbAccess() reads after a failed probe open (Linux).
    std::filesystem::path usbSysfsRoot = "/sys";
    std::filesystem::path usbDevRoot = "/dev";
};

// Test hooks of openSession(); DeviceSession::open() uses the defaults (the real library copy).
struct OpenHooks {
    ApiFactory apiFactory; // empty = JLinkLibrary::loadCopy(location, registry instance root)
    // empty = nrfutil::locate(locator) + checkVersions + NrfUtilModemUpdater
    ModemUpdaterFactory modemUpdaterFactory;
    SessionTiming timing;
    std::shared_ptr<std::atomic<std::uint64_t>> cycles; // WorkerConfig::cycles
    std::filesystem::path usbSysfsRoot = "/sys";        // WorkerConfig::usbSysfsRoot
    std::filesystem::path usbDevRoot = "/dev";
};

// The facade's private state. Owner thread, except the two queues (thread-safe) and the worker.
struct SessionInternals {
    const DeviceProfile* profile = nullptr;
    const TargetProfile* target = nullptr;
    std::optional<std::uint32_t> requestedSn; // options.probeSn (registry uniqueness check)
    std::shared_ptr<ConsoleLog> log;
    // SessionOptions::nrfUtilProvider (owner thread only; ADR-0114).
    std::function<nrfutil::LocatorOptions()> nrfUtilProvider;
    BoundedQueue<Command> commands{DeviceSession::commandQueueCapacity, OverflowPolicy::Reject};
    // When full, only console output lines are dropped (oldest first) so state changes, warnings,
    // the probe identity and the echoes of written commands always arrive in order (review R10,
    // decided 2026-10-10; R54).
    BoundedQueue<WorkerEvent> events{DeviceSession::eventQueueCapacity, &isDroppableEvent};
    // Operations (owner thread): the running one and its cancel source (ADR-0062).
    OperationId nextOperation = 1;
    std::optional<OperationId> runningOperation;
    // startConnection() was posted and the worker has not reported leaving ProbeOpen yet: a second
    // call is a no-op (review V5; the worker also ignores it on a session that is not probe-only).
    bool startConnectionPending = false;
    OperationKind runningKind = OperationKind::Reset;
    // Set by the worker thread when it died (review R14); read on the owner thread.
    std::atomic<bool> workerFailed{false};
    std::stop_source operationStop;
    // The last finished operation, so that waitForOperation() also works when the result was
    // delivered before it started waiting (one entry).
    std::optional<std::pair<OperationId, OperationResult>> lastFinished;
    std::jthread worker; // joined by DeviceSession::close(); declared last, destroyed first

    [[nodiscard]] static std::optional<std::uint32_t> requestedSerial(const DeviceSession& session);
    [[nodiscard]] static std::optional<OperationResult> finished(const DeviceSession& session,
                                                                 OperationId id);
    // After join(): takes the OperationFinishedEvent of the running operation from the event
    // queue, if the worker delivered one (review R13). True when it was found.
    bool takeQueuedResult();
    // Ends the running operation (if any) with `error` without a worker result (review R4: the
    // session was closed or its worker died). Returns the synthesized result.
    std::optional<std::pair<OperationId, OperationResult>> abandonRunning(Error error);
    // The implementation of openSession() (a friend of DeviceSession and SessionRegistry).
    [[nodiscard]] static Result<std::unique_ptr<DeviceSession>>
    open(SessionRegistry& registry, jlink::LibraryLocation location, SessionOptions options,
         OpenHooks hooks);
};

// SessionOptions::nrfUtil, or the default locator options with the user's settings (review R9).
[[nodiscard]] nrfutil::LocatorOptions nrfUtilLocator(const SessionOptions& options);

// DeviceSession::open() with test hooks.
[[nodiscard]] Result<std::unique_ptr<DeviceSession>> openSession(SessionRegistry& registry,
                                                                 jlink::LibraryLocation location,
                                                                 SessionOptions options,
                                                                 OpenHooks hooks);

// The worker loop (session_worker.cpp). Runs on SessionInternals::worker until `stop`. Sets
// `failed` when it ended with an exception (the worker died; review R14).
void runWorker(const std::stop_token& stop, WorkerConfig config, ApiFactory apiFactory,
               ModemUpdaterFactory modemUpdaterFactory, std::optional<ProbeLock> lock,
               BoundedQueue<Command>& commands, BoundedQueue<WorkerEvent>& events,
               const std::function<void()>& wake, std::atomic<bool>& failed);

} // namespace detail

} // namespace hio::device
