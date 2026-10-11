// The DeviceSession worker (ADR-0031, ADR-0034, ADR-0053, ADR-0058; plan/features/0002).
//
// Runs on the session's own std::jthread and owns everything that talks to the probe: the library
// copy (IJLinkApi), JLinkProbe, RttChannel, the line assemblers and the console file. Its loop:
// handle queued commands -> read RTT (every latency ms) -> check the target voltage every second
// while idle -> auto-reconnect every 3 s when enabled -> wait on the command queue for the rest of
// the poll interval. No QTimer and no event loop; every wait takes the stop token.

#include "hio/core/bounded_queue.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/core/version.h"
#include "hio/device/console_file_sink.h"
#include "hio/device/console_line.h"
#include "hio/device/line_assembler.h"
#include "hio/device/probe_lock.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/hw/jlink/clock.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/jlink_probe.h"
#include "hio/hw/jlink/rtt_channel.h"
#include "hio/hw/jlink/types.h"
#include "session_internal.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QString>
#include <QtLogging>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace hio::device::detail {

namespace {

using Clock = jlink::Clock;

// RTT read per cycle and channel: up to the up-buffer size, capped here (plan/features/0002).
constexpr std::size_t maxReadBytes = 64ULL * 1024;
// Commands handled per cycle before reading again (a write may take up to 2 s).
constexpr int maxCommandsPerCycle = 16;

template <class... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
};

bool isFatalOnFirstConnect(const Error& error) {
    switch (categoryOf(error.code)) {
    case ErrorCategory::Dependency:
    case ErrorCategory::Probe:
    case ErrorCategory::Io:
    case ErrorCategory::Internal:
        return true;
    default:
        return false;
    }
}

class SessionWorker {
public:
    SessionWorker(WorkerConfig config, ApiFactory apiFactory, std::optional<ProbeLock> lock,
                  BoundedQueue<Command>& commands, BoundedQueue<WorkerEvent>& events,
                  const std::function<void()>& wake)
        : m_config(std::move(config)), m_apiFactory(std::move(apiFactory)), m_lock(std::move(lock)),
          m_commands(commands), m_events(events), m_wake(wake),
          m_shellLines(Channel::Shell, m_config.target->hashPrefixIsLog),
          m_logLines(Channel::Log, false), m_readBuffer(maxReadBytes) {
        if (m_config.probeSn) {
            m_serial = m_config.probeSn;
        }
    }

    void run(const std::stop_token& stop) {
        connect(stop, true);
        while (!stop.stop_requested()) {
            const Clock::TimePoint cycleStart = clock().now();
            for (int handled = 0; handled < maxCommandsPerCycle && !stop.stop_requested();
                 ++handled) {
                auto command = m_commands.get().pop(stop, std::chrono::milliseconds(0));
                if (!command) {
                    break;
                }
                handle(std::move(*command), stop);
            }
            if (m_attached) {
                readCycle();
            }
            if (shouldAutoReconnect()) {
                qCInfo(log::lcDevice) << "Re-attaching RTT (auto-reconnect)";
                reconnect(stop, true);
            }
            if (m_api) {
                m_api->flushMessages();
            }
            flushSink();
            if (m_config.cycles) {
                m_config.cycles->fetch_add(1, std::memory_order_relaxed);
            }
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(clock().now() - cycleStart);
            const auto remaining = std::chrono::milliseconds(m_config.latencyMs) - elapsed;
            if (remaining > std::chrono::milliseconds(0)) {
                if (auto command = m_commands.get().pop(stop, remaining)) {
                    handle(std::move(*command), stop);
                }
            }
        }
        shutdown();
    }

private:
    [[nodiscard]] jlink::Clock& clock() const noexcept { return m_clock.get(); }

    // ---- events -------------------------------------------------------------------------------

    void emitEvent(WorkerEvent event) {
        if (m_events.get().push(
                std::move(event))) { // drops lines when full; fails only when closed
            m_wake.get()();
        }
    }

    void warn(Error error) {
        qCWarning(log::lcDevice).noquote()
            << "Warning:" << toQString(error.message) << toQString(error.detail);
        emitEvent(Warning{.error = std::move(error)});
    }

    // Reports a connection state, but only when the state or its error code changed (ADR-0034;
    // review fix 3: automatic retries that fail the same way stay quiet). `force` reports even an
    // unchanged state (a suspended session must still say it is stopped).
    void report(ConnectionState state, std::optional<Error> error = std::nullopt,
                bool force = false) {
        const std::optional<ErrorCode> code =
            error ? std::optional<ErrorCode>(error->code) : std::nullopt;
        if (m_reported == state && m_reportedCode == code && !force) {
            return;
        }
        m_reported = state;
        m_reportedCode = code;
        if (state == ConnectionState::Disconnected) {
            m_nextReconnect = clock().now() + m_config.timing.reconnectInterval;
        }
        std::string text(connectionStateId(state));
        if (error) {
            text += ": " + error->message;
            qCInfo(log::lcDevice).noquote()
                << "Connection" << toQString(text) << "-" << toQString(error->detail);
        } else {
            qCInfo(log::lcDevice).noquote() << "Connection" << toQString(text);
        }
        if (m_sink) {
            sinkResult(m_sink->writeConnection(text, std::chrono::system_clock::now()));
        }
        emitEvent(ConnectionEvent{.state = state, .error = std::move(error)});
    }

    void emitLine(ConsoleLine line) {
        if (m_sink) {
            sinkResult(m_sink->write(line));
        }
        emitEvent(std::move(line));
    }

    // ---- console file -------------------------------------------------------------------------

    void sinkResult(const Status& status) {
        if (!status && m_sink) {
            m_sink.reset(); // the session continues without the file
            warn(status.error());
            return;
        }
        if (m_sink) {
            if (auto warning = m_sink->takeWarning()) {
                warn(std::move(*warning)); // e.g. a failed rotation (review R26)
            }
        }
    }

    void flushSink() {
        if (m_sink) {
            sinkResult(m_sink->flush());
        }
    }

    void openSink() {
        if (m_sinkOpened || !m_config.consoleFile || !m_serial) {
            return;
        }
        m_sinkOpened = true;
        const std::string header =
            std::format("Console: {} {}, J-Link {}, hio {}", m_config.profile->displayName,
                        m_config.target->id, *m_serial, buildInfo().version);
        auto sink = ConsoleFileSink::open(*m_config.consoleFile, *m_serial, header,
                                          std::chrono::system_clock::now());
        if (!sink) {
            warn(std::move(sink).error());
            return;
        }
        m_sink = std::move(*sink);
        sinkResult({}); // a warning of the first write (e.g. a failed rotation)
    }

    // ---- connecting ---------------------------------------------------------------------------

    void connectFailed(Error error, bool initial) {
        if (error.code == ErrorCode::Cancelled) {
            return;
        }
        if (initial && (!m_config.autoReconnect || isFatalOnFirstConnect(error))) {
            m_fatal = true;
            report(ConnectionState::Stopped, std::move(error), true);
            return;
        }
        report(ConnectionState::Disconnected, std::move(error));
    }

    [[nodiscard]] Status ensureApi() {
        if (m_api) {
            return {};
        }
        auto api = m_apiFactory();
        if (!api) {
            return std::unexpected(std::move(api).error());
        }
        m_api = std::move(*api);
        m_probe = std::make_unique<jlink::JLinkProbe>(*m_api, clock(), m_config.timing.probe);
        m_rtt = std::make_unique<jlink::RttChannel>(*m_api, clock(), m_config.timing.rtt);
        return {};
    }

    // Without a serial number: the first listed probe that is not locked (ADR-0058).
    [[nodiscard]] Status chooseProbe(const std::stop_token& stop) {
        auto probes = m_probe->list();
        if (!probes) {
            return std::unexpected(std::move(probes).error());
        }
        if (probes->empty()) {
            return fail(ErrorCode::ProbeNotFound);
        }
        std::optional<Error> lockError;
        for (const jlink::ProbeInfo& candidate : *probes) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::Cancelled);
            }
            auto lock = ProbeLock::acquire(candidate.serialNumber, m_config.lockDirectory);
            if (lock) {
                m_lock = std::move(*lock);
                m_serial = candidate.serialNumber;
                qCInfo(log::lcDevice) << "Using the first free J-Link:" << candidate.serialNumber;
                return {};
            }
            const ErrorCode code = lock.error().code;
            if (code != ErrorCode::ProbeInUseByOtherProcess && code != ErrorCode::ProbeOpenFailed) {
                return std::unexpected(std::move(lock).error()); // locking is not possible
            }
            if (!lockError) {
                lockError = std::move(lock).error();
            }
        }
        return std::unexpected(std::move(lockError).value_or(makeError(ErrorCode::ProbeNotFound)));
    }

    [[nodiscard]] Status openProbe(const std::stop_token& stop) {
        if (!m_serial) {
            if (auto chosen = chooseProbe(stop); !chosen) {
                return chosen;
            }
        }
        auto info = m_probe->open(m_serial);
        if (!info) {
            return std::unexpected(std::move(info).error());
        }
        emitEvent(ProbeIdentified{.probe = *info});
        if (!m_firmwareChecked) {
            m_firmwareChecked = true;
            const jlink::FirmwareStatus status = m_probe->firmwareStatus();
            // Unknown is not reported: nothing is known to be wrong (review R11).
            if (status == jlink::FirmwareStatus::Outdated ||
                status == jlink::FirmwareStatus::Newer) {
                Error error = makeError(
                    ErrorCode::ProbeFirmwareOutdated,
                    std::format("J-Link {} firmware: {}", info->serialNumber, info->firmware));
                if (status == jlink::FirmwareStatus::Newer) {
                    error.message =
                        QCoreApplication::translate(
                            "hio::device", "The J-Link firmware is newer than the J-Link library.")
                            .toStdString();
                }
                warn(std::move(error));
            }
        }
        openSink();
        return {};
    }

    [[nodiscard]] Status attachRtt(const std::stop_token& stop) {
        const jlink::RttNames names{.terminal = std::string(m_config.target->rttTerminal),
                                    .logger = std::string(m_config.target->rttLogger)};
        auto layout = m_rtt->attach(names, stop);
        if (!layout) {
            return std::unexpected(std::move(layout).error());
        }
        m_shellLines.reset();
        m_logLines.reset();
        if (!layout->loggerUp && !names.logger.empty() && !m_loggerWarned) {
            m_loggerWarned = true;
            Error error = makeError(ErrorCode::TargetRttBufferNotFound, "continuing without logs");
            error.message = QCoreApplication::translate("hio::device",
                                                        "RTT buffer \"%1\" not found; the device "
                                                        "log is not shown.")
                                .arg(toQString(names.logger))
                                .toStdString();
            warn(std::move(error));
        }
        m_attached = true;
        m_nextPowerCheck = clock().now() + m_config.timing.powerCheckInterval;
        return {};
    }

    // Library -> probe -> target -> RTT. `initial`: the first connect of the session, where
    // library, lock and probe errors (or any error without auto-reconnect) are fatal.
    // `automatic`: an auto-reconnect retry, which does not report Connecting (review fix 3).
    void connect(const std::stop_token& stop, bool initial, bool automatic = false) {
        if (!automatic) {
            report(ConnectionState::Connecting);
        }
        const auto step = [&](Status status) {
            if (!status) {
                connectFailed(std::move(status).error(), initial);
                return false;
            }
            return !stop.stop_requested();
        };
        if (!step(ensureApi())) {
            return;
        }
        if (!m_probe->isOpen() && !step(openProbe(stop))) {
            return;
        }
        m_probe->setSpeed(m_config.speedKhz);
        if (!step(m_probe->selectSwd())) {
            return;
        }
        if (!step(m_probe->connect(m_config.target->jlinkDevice,
                                   m_config.target->minTargetMillivolts, m_seenPower, stop))) {
            return;
        }
        readPowerAfterConnect();
        if (m_config.resetOnOpen && !m_resetDone) {
            m_resetDone = true;
            if (!step(m_probe->reset(false))) {
                return;
            }
        }
        if (!step(attachRtt(stop))) {
            return;
        }
        report(ConnectionState::Connected);
    }

    void detach() {
        if (m_rtt) {
            m_rtt->stop();
        }
        m_attached = false;
        if (m_probe) {
            m_probe->close();
        }
    }

    // The next automatic attempt is due one reconnect interval after this attempt ENDED,
    // whatever its outcome (review R31): an attempt that fails like the previous one is not
    // reported again (report() de-duplicates and does not move the timer), and an attempt can
    // take longer than the interval (RTT attach deadline 15 s), so a timer set before the attempt
    // would let the retries run back to back.
    void reconnect(const std::stop_token& stop, bool automatic = false) {
        detach();
        connect(stop, false, automatic);
        m_nextReconnect = clock().now() + m_config.timing.reconnectInterval;
    }

    [[nodiscard]] bool shouldAutoReconnect() {
        if (!m_config.autoReconnect || m_suspended || m_halted || m_fatal ||
            m_reported != ConnectionState::Disconnected || clock().now() < m_nextReconnect) {
            return false;
        }
        // Retries stay off the probe while the board has no power (each attempt takes seconds).
        // A probe that does not answer at all (USB unplugged) is lost: the retry closes it and
        // opens it again, so a replugged probe is found (review fix 2).
        // The gate also works before any healthy reading when the last attempt failed for lack
        // of power (first connect with --auto-reconnect; decided 2026-10-09).
        const bool powerKnownToMatter = m_seenPower || m_reportedCode == ErrorCode::TargetNoPower;
        if (powerKnownToMatter && m_probe && m_probe->isOpen()) {
            const auto status = m_probe->hardwareStatus();
            if (status && status->targetMillivolts < m_config.target->minTargetMillivolts) {
                m_nextReconnect = clock().now() + m_config.timing.reconnectInterval;
                return false;
            }
        }
        return true;
    }

    // ---- reading ------------------------------------------------------------------------------

    void readCycle() {
        bool gotData = false;
        std::optional<Error> failure;
        const jlink::RttLayout& layout = m_rtt->layout();
        const auto readChannel = [&](const jlink::RttBuffer& buffer, LineAssembler& assembler) {
            if (buffer.index < 0 || buffer.size == 0) {
                return;
            }
            const std::size_t length = std::min<std::size_t>(buffer.size, m_readBuffer.size());
            auto bytes = m_rtt->read(buffer.index, std::span(m_readBuffer).first(length));
            if (!bytes) {
                failure = std::move(bytes).error();
                return;
            }
            if (*bytes == 0) {
                return;
            }
            gotData = true;
            for (ConsoleLine& line : assembler.feed(std::span(m_readBuffer).first(*bytes),
                                                    std::chrono::system_clock::now())) {
                emitLine(std::move(line));
            }
        };
        readChannel(layout.terminalUp, m_shellLines);
        if (layout.loggerUp) {
            readChannel(*layout.loggerUp, m_logLines);
        }
        // An empty read is no evidence of a live target (an unpowered board reads empty), so
        // only real data clears a disconnect (ADR-0034).
        // The power check runs every second also while data arrives (review R13): a target
        // that keeps printing from its own buffer must not hide a power loss.
        if (failure) {
            report(ConnectionState::Disconnected, std::move(failure));
            return;
        }
        const bool powered = checkPower();
        if (gotData && powered) {
            report(ConnectionState::Connected);
        }
    }

    // A VTref reading right after the target connected: a target that connects is powered, so a
    // healthy reading arms the power-loss detection from the start (review R13).
    void readPowerAfterConnect() {
        const auto status = m_probe->hardwareStatus();
        if (status && status->targetMillivolts >= m_config.target->minTargetMillivolts) {
            m_seenPower = true;
            m_powered = true;
        } else if (status && !m_seenPower) {
            m_powered = true; // a probe that always reads 0 mV (review R33)
        }
        m_nextPowerCheck = clock().now() + m_config.timing.powerCheckInterval;
    }

    // Every powerCheckInterval: reads VTref and reports a power loss. Returns false while the
    // target is known to be unpowered (or the probe does not answer).
    bool checkPower() {
        const Clock::TimePoint now = clock().now();
        if (now < m_nextPowerCheck) {
            return m_powered;
        }
        m_nextPowerCheck = now + m_config.timing.powerCheckInterval;
        const auto status = m_probe->hardwareStatus();
        if (!status) {
            m_powered = false;
            report(ConnectionState::Disconnected, status.error());
            return false;
        }
        if (status->targetMillivolts >= m_config.target->minTargetMillivolts) {
            m_seenPower = true;
            m_powered = true;
        } else if (!m_seenPower) {
            // A probe that cannot measure VTref (always 0 mV) answered: nothing says the target
            // lost power, so an earlier failed status read no longer blocks Connected (R33).
            m_powered = true;
        } else {
            m_powered = false;
            // Trusted only after a healthy reading: a probe that cannot measure VTref and always
            // reports 0 never trips this.
            Error error = makeError(ErrorCode::TargetNoPower,
                                    std::format("VTref {} mV", status->targetMillivolts));
            error.message =
                QCoreApplication::translate("hio::device", "The device has no power (%1 mV).")
                    .arg(status->targetMillivolts)
                    .toStdString();
            report(ConnectionState::Disconnected, std::move(error));
        }
        return m_powered;
    }

    // ---- commands -----------------------------------------------------------------------------

    // target.not_connected for a command while detached (review R18).
    [[nodiscard]] Error notConnected(std::string_view what) const {
        Error error = makeError(ErrorCode::TargetNotConnected,
                                std::format("{}: the session is not attached", what));
        // Only when the session will really re-attach by itself (review R35); otherwise the
        // default suggestion "Reconnect the console (F4)." stays.
        if (m_config.autoReconnect && !m_halted && !m_suspended && !m_fatal) {
            error.suggestion =
                QCoreApplication::translate("hio::device", "Wait for the console to reconnect.")
                    .toStdString();
        }
        return error;
    }

    void sendLine(std::string line, const std::stop_token& stop) {
        if (!m_attached) {
            warn(notConnected("command not sent"));
            return;
        }
        std::string data = line + "\n";
        const auto bytes = std::as_bytes(std::span(data));
        const Status status = m_rtt->write(m_rtt->layout().terminalDown.index, bytes, stop);
        if (!status) {
            if (status.error().code == ErrorCode::Cancelled) {
                return;
            }
            if (status.error().code == ErrorCode::TargetRttBufferNotFound) {
                warn(status.error()); // the buffer is unsized, the link itself may be fine
                return;
            }
            report(ConnectionState::Disconnected, status.error());
            return;
        }
        ConsoleLine echo;
        echo.channel = Channel::Shell;
        echo.dir = Direction::In;
        echo.time = std::chrono::system_clock::now();
        echo.text = std::move(line);
        emitLine(std::move(echo));          // only after it was written (ADR-0034)
        report(ConnectionState::Connected); // a write that landed proves the link works
    }

    void resetTarget(bool halt, const std::stop_token& stop) {
        if (!m_probe || !m_probe->isOpen()) {
            warn(notConnected("reset not done"));
            return;
        }
        if (!halt) {
            m_halted = false; // a reset without halt lets the CPU run (review R25)
        }
        // Stop RTT without reporting a disconnect; a plain reset re-attaches below.
        m_rtt->stop();
        m_attached = false;
        if (const Status status = m_probe->reset(halt); !status) {
            report(ConnectionState::Disconnected, status.error());
            return;
        }
        if (halt) {
            // The CPU stays halted: no automatic re-attach (like a suspend) until an explicit
            // reconnect, resume or reset without halt (ADR-0034 "unless halted"; review R25).
            m_halted = true;
            report(ConnectionState::Disconnected);
            return;
        }
        if (const Status status = attachRtt(stop); !status) {
            if (status.error().code != ErrorCode::Cancelled) {
                report(ConnectionState::Disconnected, status.error());
            }
            return;
        }
        report(ConnectionState::Connected);
    }

    void handle(Command command, const std::stop_token& stop) {
        std::visit(Overloaded{
                       [&](SendLineCommand& send) { sendLine(std::move(send.line), stop); },
                       [&](const ResetCommand& reset) { resetTarget(reset.halt, stop); },
                       [&](const ReconnectCommand&) {
                           m_suspended = false;
                           m_halted = false;
                           m_fatal = false;
                           reconnect(stop);
                       },
                       [&](const SuspendCommand&) {
                           m_suspended = true;
                           detach();
                           report(ConnectionState::Stopped, std::nullopt, true);
                       },
                       [&](const ResumeCommand&) {
                           m_suspended = false;
                           m_halted = false;
                           m_fatal = false;
                           reconnect(stop);
                       },
                   },
                   command);
    }

    void shutdown() {
        detach();
        if (m_sink) {
            sinkResult(
                m_sink->writeConnection(std::string(connectionStateId(ConnectionState::Stopped)),
                                        std::chrono::system_clock::now()));
            flushSink();
        }
        m_sink.reset();
        m_rtt = nullptr;
        m_probe = nullptr;
        m_api = nullptr; // unloads the library copy
        m_lock.reset();
        qCInfo(log::lcDevice) << "Session worker stopped";
    }

    WorkerConfig m_config;
    ApiFactory m_apiFactory;
    std::optional<ProbeLock> m_lock;
    std::reference_wrapper<BoundedQueue<Command>> m_commands;
    std::reference_wrapper<BoundedQueue<WorkerEvent>> m_events;
    std::reference_wrapper<const std::function<void()>> m_wake;
    std::reference_wrapper<jlink::SystemClock> m_clock{jlink::SystemClock::instance()};

    std::unique_ptr<jlink::IJLinkApi> m_api;
    std::unique_ptr<jlink::JLinkProbe> m_probe; // uses *m_api; destroyed before it
    std::unique_ptr<jlink::RttChannel> m_rtt;   // uses *m_api; destroyed before it
    std::optional<std::uint32_t> m_serial;
    std::unique_ptr<ConsoleFileSink> m_sink;
    LineAssembler m_shellLines;
    LineAssembler m_logLines;
    std::vector<std::byte> m_readBuffer; // maxReadBytes, allocated once

    std::optional<ConnectionState> m_reported;
    std::optional<ErrorCode> m_reportedCode; // error code of the last reported state
    Clock::TimePoint m_nextPowerCheck;
    Clock::TimePoint m_nextReconnect;
    bool m_attached = false;
    bool m_suspended = false;
    bool m_halted = false; // after reset(halt = true): no auto-reconnect (review R25)
    bool m_fatal = false;
    bool m_seenPower = false;
    bool m_powered = true; // false after a power loss (or an unanswered status read)
    bool m_firmwareChecked = false;
    bool m_loggerWarned = false;
    bool m_resetDone = false;
    bool m_sinkOpened = false;
};

} // namespace

void runWorker(const std::stop_token& stop, WorkerConfig config, ApiFactory apiFactory,
               std::optional<ProbeLock> lock, BoundedQueue<Command>& commands,
               BoundedQueue<WorkerEvent>& events, const std::function<void()>& wake) {
    try {
        SessionWorker worker(std::move(config), std::move(apiFactory), std::move(lock), commands,
                             events, wake);
        worker.run(stop);
    } catch (...) {
        // No exception crosses the thread boundary (ADR-0007): report it as a fatal error.
        if (events.push(ConnectionEvent{.state = ConnectionState::Stopped,
                                        .error = makeError(ErrorCode::InternalUnexpected,
                                                           "exception in the session worker")})) {
            wake();
        }
    }
}

} // namespace hio::device::detail
