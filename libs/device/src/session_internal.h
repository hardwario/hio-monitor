#pragma once

// Internals of DeviceSession shared by the facade (device_session.cpp), the worker
// (session_worker.cpp) and the unit tests (which inject an IJLinkApi factory and short timings).

#include "hio/core/bounded_queue.h"
#include "hio/core/result.h"
#include "hio/device/console_file_sink.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/device_session.h"
#include "hio/device/probe_lock.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/jlink_probe.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/rtt_channel.h"
#include "hio/hw/jlink/types.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <variant>

namespace hio::device {

class SessionRegistry;

namespace detail {

// ---- commands (owner thread -> worker) --------------------------------------------------------

struct SendLineCommand {
    std::string line;
};
struct ResetCommand {
    bool halt = false;
};
struct ReconnectCommand {};
struct SuspendCommand {};
struct ResumeCommand {};

using Command =
    std::variant<SendLineCommand, ResetCommand, ReconnectCommand, SuspendCommand, ResumeCommand>;

// ---- events (worker -> owner thread) ----------------------------------------------------------

// The probe the worker opened; sets DeviceSession::probe() before the next connection event.
struct ProbeIdentified {
    jlink::ProbeInfo probe;
};

using WorkerEvent = std::variant<ConsoleLine, ConnectionEvent, Warning, ProbeIdentified>;

// The events the event queue may drop when it is full.
[[nodiscard]] inline bool isConsoleLine(const WorkerEvent& event) {
    return std::holds_alternative<ConsoleLine>(event);
}

// ---- configuration ----------------------------------------------------------------------------

using ApiFactory = std::function<Result<std::unique_ptr<jlink::IJLinkApi>>()>;

struct SessionTiming {
    std::chrono::milliseconds powerCheckInterval{1000}; // ADR-0034
    std::chrono::milliseconds reconnectInterval{3000};  // ADR-0034
    jlink::ProbeTiming probe;
    jlink::RttTiming rtt;
};

struct WorkerConfig {
    const DeviceProfile* profile = nullptr;
    const TargetProfile* target = nullptr;
    std::optional<std::uint32_t> probeSn;
    int speedKhz = 0;
    int latencyMs = 0;
    bool autoReconnect = false;
    bool resetOnOpen = false;
    std::optional<ConsoleFileConfig> consoleFile; // nullopt = no console file
    std::filesystem::path lockDirectory;
    SessionTiming timing;
    // Test hook (review R40): incremented once per worker loop iteration, so a test can wait for
    // a number of cycles instead of a fixed time. nullptr = not counted (production).
    std::shared_ptr<std::atomic<std::uint64_t>> cycles;
};

// Test hooks of openSession(); DeviceSession::open() uses the defaults (the real library copy).
struct OpenHooks {
    ApiFactory apiFactory; // empty = JLinkLibrary::loadCopy(location, registry instance root)
    SessionTiming timing;
    std::shared_ptr<std::atomic<std::uint64_t>> cycles; // WorkerConfig::cycles
};

// The facade's private state. Owner thread, except the two queues (thread-safe) and the worker.
struct SessionInternals {
    const DeviceProfile* profile = nullptr;
    const TargetProfile* target = nullptr;
    std::optional<std::uint32_t> requestedSn; // options.probeSn (registry uniqueness check)
    std::shared_ptr<ConsoleLog> log;
    BoundedQueue<Command> commands{DeviceSession::commandQueueCapacity, OverflowPolicy::Reject};
    // When full, only console lines are dropped (oldest first) so state changes, warnings and the
    // probe identity always arrive in order (review R10, decided 2026-10-10).
    BoundedQueue<WorkerEvent> events{DeviceSession::eventQueueCapacity, &isConsoleLine};
    std::jthread worker; // joined by DeviceSession::close(); declared last, destroyed first

    [[nodiscard]] static std::optional<std::uint32_t> requestedSerial(const DeviceSession& session);
    // The implementation of openSession() (a friend of DeviceSession and SessionRegistry).
    [[nodiscard]] static Result<std::unique_ptr<DeviceSession>>
    open(SessionRegistry& registry, jlink::LibraryLocation location, SessionOptions options,
         OpenHooks hooks);
};

// DeviceSession::open() with test hooks.
[[nodiscard]] Result<std::unique_ptr<DeviceSession>> openSession(SessionRegistry& registry,
                                                                 jlink::LibraryLocation location,
                                                                 SessionOptions options,
                                                                 OpenHooks hooks);

// The worker loop (session_worker.cpp). Runs on SessionInternals::worker until `stop`.
void runWorker(const std::stop_token& stop, WorkerConfig config, ApiFactory apiFactory,
               std::optional<ProbeLock> lock, BoundedQueue<Command>& commands,
               BoundedQueue<WorkerEvent>& events, const std::function<void()>& wake);

} // namespace detail

} // namespace hio::device
