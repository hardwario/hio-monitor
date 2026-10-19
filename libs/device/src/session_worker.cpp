// The DeviceSession worker (ADR-0031, ADR-0034, ADR-0053, ADR-0058; plan/features/0002).
//
// Runs on the session's own std::jthread and owns everything that talks to the probe: the library
// copy (IJLinkApi), JLinkProbe, RttChannel, the line assemblers and the console file. Its loop:
// handle queued commands -> read RTT (every latency ms) -> check the target voltage every second
// while idle -> auto-reconnect every 3 s when enabled -> wait on the command queue for the rest of
// the poll interval. No QTimer and no event loop; every wait takes the stop token.

#include "flash_workflows.h"
#include "hio/core/bounded_queue.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/core/version.h"
#include "hio/device/console_file_sink.h"
#include "hio/device/console_line.h"
#include "hio/device/line_assembler.h"
#include "hio/device/operation.h"
#include "hio/device/probe_lock.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/device/session_key.h"
#include "hio/diag/check.h"
#include "hio/diag/usb_access.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/program_progress.h"
#include "hio/hw/jlink/clock.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/jlink_probe.h"
#include "hio/hw/jlink/programmer.h"
#include "hio/hw/jlink/rtt_channel.h"
#include "hio/hw/jlink/types.h"
#include "hio/hw/nrfutil/nrfutil.h"
#include "operation_reporter.h"
#include "session_internal.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QString>
#include <QStringList>
#include <QtContainerFwd>
#include <QtLogging>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
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
    SessionWorker(WorkerConfig config, ApiFactory apiFactory, ModemUpdaterFactory modemFactory,
                  std::optional<ProbeLock> lock, BoundedQueue<Command>& commands,
                  BoundedQueue<WorkerEvent>& events, const std::function<void()>& wake)
        : m_config(std::move(config)), m_apiFactory(std::move(apiFactory)),
          m_modemFactory(std::move(modemFactory)), m_lock(std::move(lock)), m_commands(commands),
          m_events(events), m_wake(wake),
          m_shellLines(Channel::Shell, m_config.target->hashPrefixIsLog),
          m_logLines(Channel::Log, false), m_readBuffer(maxReadBytes) {
        if (m_config.probeSn) {
            m_serial = m_config.probeSn;
        }
        m_probeOnly = !m_config.connectTarget;
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
        emitEvent(ConnectionEvent{
            .state = state, .error = std::move(error), .targetMillivolts = m_lastMillivolts});
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
            reportConsoleFile();
            warn(status.error());
            return;
        }
        if (m_sink) {
            reportConsoleFile(); // a write may have continued in the next .partN file
            if (auto warning = m_sink->takeWarning()) {
                warn(std::move(*warning)); // e.g. a failed rotation (review R26)
            }
        }
    }

    // DeviceSession::consoleFile() (amended by plan 0005): reported when the file changes.
    void reportConsoleFile() {
        static const std::filesystem::path none;
        const std::filesystem::path& current = m_sink ? m_sink->currentFile() : none;
        if (current.native() == m_reportedConsoleFile.native()) { // per line: a string compare
            return;
        }
        m_reportedConsoleFile = current;
        emitEvent(ConsoleFileChanged{.file = current});
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
        auto sink = ConsoleFileSink::open(*m_config.consoleFile, ProbeKey{.sn = *m_serial}, header,
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
        m_rtt = std::make_unique<jlink::RttChannel>(*m_api, clock(), m_config.timing.rtt,
                                                    m_config.speedKhz);
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
                return std::unexpected(usbAccessOr(std::move(chosen).error()));
            }
        }
        auto info = m_probe->open(m_serial);
        if (!info) {
            return std::unexpected(usbAccessOr(std::move(info).error()));
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
        if (!m_probeOnly) {
            // A probe-only session (a wizard's Diagnose) opens the console file only when it is
            // promoted or runs another operation (review V12): no header-only file, no rotation.
            openSink();
        }
        return {};
    }

    // ADR-0072 (Linux): a probe that cannot be opened because this user has no access to its USB
    // device node is reported as usb.access_denied instead of probe.open_failed, and instead of
    // probe.not_found (review V7): without access the library may not list the probe at all.
    [[nodiscard]] Error usbAccessOr(Error error) const {
        if (error.code != ErrorCode::ProbeOpenFailed && error.code != ErrorCode::ProbeNotFound) {
            return error;
        }
        const auto devices = diag::checkUsbAccess(m_config.usbSysfsRoot, m_config.usbDevRoot);
        if (!devices) {
            return error;
        }
        if (auto denied = diag::usbAccessDeniedError(*devices)) {
            denied->detail += "; " + error.detail;
            return std::move(*denied);
        }
        return error;
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
        if (m_probeOnly) {
            // ADR-0069: a probe-only session never connects the target by itself.
            report(ConnectionState::ProbeOpen);
            return;
        }
        openSink(); // a promoted probe-only session (review V12); no-op when open
        // Automatic unless a kHz value was given; connect() sets it again after `Device =`, as
        // pylink's JLink.connect() (ADR-0090).
        m_probe->setSpeed(m_config.speedKhz);
        if (!step(m_probe->selectSwd())) {
            return;
        }
        if (!step(connectDevice(*m_config.target, m_config.target->jlinkDevice, stop))) {
            return;
        }
        readPowerAfterConnect();
        if (m_config.resetOnOpen && !m_resetDone) {
            m_resetDone = true;
            if (!step(m_probe->reset(false))) {
                return;
            }
        }
        if (m_config.attachRtt && !step(attachRtt(stop))) {
            return;
        }
        report(ConnectionState::Connected);
    }

    // Connects the probe to `target` with the J-Link device name `device`.
    [[nodiscard]] Status connectDevice(const TargetProfile& target, std::string_view device,
                                       const std::stop_token& stop,
                                       std::optional<int> attempts = std::nullopt) {
        m_probeTarget = nullptr;
        if (Status connected =
                m_probe->connect(device, target.minTargetMillivolts, m_seenPower, stop, attempts);
            !connected) {
            return connected;
        }
        m_probeTarget = &target;
        m_connectedDevice = std::string(device);
        return {};
    }

    void detach() {
        if (m_rtt) {
            m_rtt->stop();
        }
        m_attached = false;
        if (m_probe) {
            m_probe->close();
        }
        m_probeTarget = nullptr;
        m_lastMillivolts.reset();
    }

    // The next automatic attempt is due one reconnect interval after this attempt ENDED,
    // whatever its outcome (review R31): an attempt that fails like the previous one is not
    // reported again (report() de-duplicates and does not move the timer), and an attempt can
    // take longer than the interval (RTT attach deadline 15 s, up to 60 s at a fixed low speed,
    // ADR-0110), so a timer set before the attempt
    // would let the retries run back to back.
    void reconnect(const std::stop_token& stop, bool automatic = false) {
        detach();
        connect(stop, false, automatic);
        m_nextReconnect = clock().now() + m_config.timing.reconnectInterval;
    }

    [[nodiscard]] bool shouldAutoReconnect() {
        if (!m_config.autoReconnect || m_probeOnly || m_suspended || m_halted || m_fatal ||
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
    // ADR-0091: the voltage reported with connection events, from the readings below (no extra
    // polling). 0 mV without a healthy reading before means "cannot measure" (review R21/R33).
    void rememberVoltage(const std::optional<jlink::HardwareStatus>& status) {
        if (!status || (status->targetMillivolts <= 0 && !m_seenPower)) {
            m_lastMillivolts.reset();
            return;
        }
        m_lastMillivolts = status->targetMillivolts;
    }

    void readPowerAfterConnect() {
        const auto status = m_probe->hardwareStatus();
        rememberVoltage(status ? std::optional(*status) : std::nullopt);
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
        rememberVoltage(status ? std::optional(*status) : std::nullopt);
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

    // ---- operations (plan/features/0003, ADR-0057, ADR-0062) -----------------------------------

    void emitProgress(OperationId id, const OperationProgress& progress) {
        emitEvent(OperationProgressEvent{.id = id, .progress = progress});
    }

    void flashLog(const std::string& text) {
        qCInfo(log::lcFlash).noquote() << toQString(text);
        if (m_sink) {
            sinkResult(m_sink->writeConnection(text, std::chrono::system_clock::now()));
        }
    }

    // The programmer for `target`: its own chip family first, the other targets' after (so that
    // identify() recognizes the other MCU for target.wrong_family).
    [[nodiscard]] jlink::ProgrammerConfig programmerConfigFor(const TargetProfile& target) const {
        jlink::ProgrammerConfig config;
        config.families.push_back(target.chipFamily);
        for (const TargetProfile& other : m_config.profile->targets) {
            if (other.chipFamily.ficrPart != target.chipFamily.ficrPart) {
                config.families.push_back(other.chipFamily);
            }
        }
        config.timing = m_config.timing.programmer;
        return config;
    }

    // Library, probe and the connection to `target` (Connecting). `requireTarget` = false (recover)
    // continues when the target does not connect (a protected chip): CoreSight still works.
    [[nodiscard]] Status prepareProbe(const TargetProfile& target, bool requireTarget,
                                      const std::stop_token& stop) {
        if (Status api = ensureApi(); !api) {
            return api;
        }
        if (!m_probe->isOpen()) {
            if (Status opened = openProbe(stop); !opened) {
                return opened;
            }
        }
        m_probe->setSpeed(m_config.speedKhz);
        if (Status swd = m_probe->selectSwd(); !swd) {
            return swd;
        }
        if (m_probeTarget == &target && m_api->isConnected()) {
            m_fatal = false;
            return {};
        }
        Status connected = connectDevice(target, target.jlinkDevice, stop);
        if (!connected && requireTarget) {
            return connected;
        }
        if (!connected) {
            flashLog("Target not connected (" + connected.error().detail + "); continuing");
        }
        m_fatal = false;
        return {};
    }

    // The probe is released for nRF Util: closed, the library copy stays loaded (ADR-0016).
    void releaseProbe() {
        if (m_probe && m_probe->isOpen()) {
            m_probe->close();
            flashLog("J-Link released for nRF Util");
        }
        m_probeTarget = nullptr;
    }

    // Opens the probe again after nRF Util (also after a failure or cancel).
    [[nodiscard]] Status reopenProbe(const std::stop_token& stop) {
        if (Status api = ensureApi(); !api) {
            return api;
        }
        if (m_probe->isOpen()) {
            return {};
        }
        auto info = m_probe->open(m_serial);
        if (!info) {
            return std::unexpected(usbAccessOr(std::move(info).error()));
        }
        (void)stop;
        return {};
    }

    // Step "Modem": nRF Util with the probe released.
    // `locator`: the options the command carries (ADR-0114), else the snapshot of open().
    [[nodiscard]] Status runModem(const std::filesystem::path& modemZip,
                                  const std::optional<nrfutil::LocatorOptions>& locator,
                                  OperationReporter& reporter, const std::stop_token& stop) {
        auto updater = m_modemFactory
                           ? m_modemFactory(locator ? *locator : m_config.nrfUtil)
                           : Result<std::unique_ptr<nrfutil::IModemUpdater>>(
                                 std::unexpected(makeError(ErrorCode::DependencyNrfUtilNotFound)));
        if (!updater) {
            return std::unexpected(std::move(updater).error());
        }
        if (!m_serial) {
            // The probe was never opened (e.g. the first connect failed): find it now.
            if (Status api = ensureApi(); !api) {
                return api;
            }
            if (Status opened = openProbe(stop); !opened) {
                return opened;
            }
        }
        reporter.phase(ProgramPhase::Modem);
        releaseProbe();
        flashLog("Updating the modem firmware: " + toUtf8(modemZip));
        Status updated = (*updater)->updateModem(
            modemZip, m_serial.value_or(0),
            [&reporter](const firmware::ProgramProgress& progress) { reporter.update(progress); },
            stop);
        // resume(): also after a failure or cancel.
        if (Status reopened = reopenProbe(stop); !reopened && updated) {
            updated = std::move(reopened);
        }
        flashLog(updated ? std::string("Modem firmware updated")
                         : "Modem firmware update failed: " + updated.error().detail);
        return updated;
    }

    // Moves the probe back to the console target and re-attaches RTT unless the CPU was left
    // halted (ADR-0057, ADR-0062). A failed restore reports Disconnected with the error.
    void restoreAfterOperation(bool crossTarget, bool cpuHalted, OperationReporter& reporter,
                               const std::stop_token& stop) {
        const auto restoreFailed = [this](Error error) {
            report(ConnectionState::Disconnected, std::move(error));
        };
        // Connected again: a session that had stopped fatally (e.g. Diagnose on a Stopped session)
        // works again, so auto-reconnect applies again (review V6).
        const auto restored = [this] {
            m_fatal = false;
            report(ConnectionState::Connected);
        };
        if (m_probeOnly) {
            // ADR-0069: a probe-only session keeps the probe open and the target as it is.
            if (Status reopened = reopenProbe(stop); !reopened) {
                report(ConnectionState::Stopped, std::move(reopened).error(), true);
                return;
            }
            report(ConnectionState::ProbeOpen);
            return;
        }
        if (crossTarget) {
            reporter.phase(ProgramPhase::Restoring);
        }
        if (Status reopened = reopenProbe(stop); !reopened) {
            restoreFailed(std::move(reopened).error());
            return;
        }
        const bool targetLost = m_probeTarget != m_config.target || !m_api->isConnected();
        if (targetLost) {
            m_probe->setSpeed(m_config.speedKhz);
            if (Status swd = m_probe->selectSwd(); !swd) {
                restoreFailed(std::move(swd).error());
                return;
            }
            if (Status connected =
                    connectDevice(*m_config.target, m_config.target->jlinkDevice, stop);
                !connected) {
                restoreFailed(std::move(connected).error());
                return;
            }
            cpuHalted = cpuHalted && !crossTarget; // the console target was not touched
        }
        if (cpuHalted) {
            // Like M1's reset(halt): no automatic re-attach until an explicit reconnect, resume or
            // reset with go (ADR-0034, review R25).
            m_halted = true;
            report(m_config.attachRtt ? ConnectionState::Disconnected : ConnectionState::Connected);
            return;
        }
        m_halted = false;
        if (!m_config.attachRtt) {
            restored();
            return;
        }
        if (Status attached = attachRtt(stop); !attached) {
            if (attached.error().code != ErrorCode::Cancelled) {
                report(ConnectionState::Disconnected, attached.error());
            }
            return;
        }
        restored();
    }

    // The state of one running operation (runOperation()).
    struct OperationRun {
        const OperationCommand& command;
        const TargetProfile& target;
        bool crossTarget;
        std::stop_token stop;
        OperationReporter& reporter;
        WorkflowContext context;
        std::optional<jlink::JLinkProgrammer> programmer;
    };

    // Connecting (after SwitchingTarget for another target): probe and target ready, programmer
    // created. A target that does not connect may be read-back protected: its CTRL-AP is asked.
    [[nodiscard]] Status prepareOperation(OperationRun& run, bool requireTarget) {
        if (run.crossTarget && run.command.kind != OperationKind::LteBundle) {
            run.reporter.phase(ProgramPhase::SwitchingTarget);
        }
        Status prepared = prepareProbe(run.target, requireTarget, run.stop);
        run.reporter.phase(ProgramPhase::Connecting);
        if (m_api && m_probe && m_probe->isOpen()) {
            run.programmer.emplace(*m_api, *m_probe, clock(), programmerConfigFor(run.target));
            run.context.programmer = &*run.programmer;
        }
        if (!prepared) {
            // A target that does not connect may be read-back protected, or the probe may sit on
            // the other MCU's connector: ask the CTRL-APs (reviews R2, R15).
            if (run.programmer && prepared.error().code == ErrorCode::TargetConnectFailed) {
                if (auto chip = identifyTarget(run.context, false);
                    !chip && (chip.error().code == ErrorCode::TargetProtected ||
                              chip.error().code == ErrorCode::TargetWrongFamily)) {
                    return std::unexpected(std::move(chip).error());
                }
            }
            return prepared;
        }
        run.context.connectedDevice = m_connectedDevice;
        run.context.connectAs = [this, &run](std::string_view device) {
            return connectDevice(run.target, device, run.stop);
        };
        return {};
    }

    // Keeps the identified chip of a workflow result (ChipInfo or optional<ChipInfo>).
    template <class T> static Status keepChip(const Result<T>& chip, OperationResult& result) {
        if (!chip) {
            return std::unexpected(chip.error());
        }
        result.chip = *chip;
        return {};
    }

    [[nodiscard]] Status runFlashStep(OperationRun& run, const firmware::FirmwareImage& image,
                                      OperationResult& result) {
        if (Status prepared = prepareOperation(run, true); !prepared) {
            return prepared;
        }
        return keepChip(runFlash(run.context, image, run.command.flash), result);
    }

    [[nodiscard]] Status executeOperation(OperationRun& run, OperationResult& result) {
        const OperationCommand& command = run.command;
        switch (command.kind) {
        case OperationKind::Flash:
            return runFlashStep(run, *command.image, result);
        case OperationKind::EraseCode:
        case OperationKind::EraseAll: {
            if (Status prepared = prepareOperation(run, true); !prepared) {
                return prepared;
            }
            return keepChip(runErase(run.context, command.kind == OperationKind::EraseCode
                                                      ? EraseScope::Code
                                                      : EraseScope::All),
                            result);
        }
        case OperationKind::Recover: {
            if (Status prepared = prepareOperation(run, false); !prepared) {
                return prepared;
            }
            return keepChip(runRecover(run.context,
                                       [this, &run]() -> Status {
                                           return connectDevice(run.target, run.target.jlinkDevice,
                                                                run.stop);
                                       }),
                            result);
        }
        case OperationKind::Reset:
            if (Status prepared = prepareOperation(run, true); !prepared) {
                return prepared;
            }
            return runReset(run.context, command.reset);
        case OperationKind::ReadMemory: {
            if (Status prepared = prepareOperation(run, true); !prepared) {
                return prepared;
            }
            auto data = runReadMemory(run.context, command.address, command.size);
            if (!data) {
                return std::unexpected(std::move(data).error());
            }
            result.data = std::move(*data);
            return {};
        }
        case OperationKind::Modem:
            if (run.crossTarget) {
                run.reporter.phase(ProgramPhase::SwitchingTarget);
            }
            return runModem(command.modemZip, command.nrfUtil, run.reporter, run.stop);
        case OperationKind::LteBundle: {
            if (run.crossTarget) {
                run.reporter.phase(ProgramPhase::SwitchingTarget);
            }
            if (Status modem =
                    runModem(command.bundle->modemZip, command.nrfUtil, run.reporter, run.stop);
                !modem) {
                return modem; // a failed or cancelled step 1 skips step 2 (ADR-0062)
            }
            run.reporter.step(2, 2);
            return runFlashStep(run, command.bundle->application, result);
        }
        case OperationKind::Diagnose: // runDiagnose()
            break;
        }
        return std::unexpected(makeError(ErrorCode::InternalUnexpected, "unknown operation"));
    }

    void runOperation(const OperationCommand& command, const std::stop_token& sessionStop) {
        std::stop_source operationStop;
        const std::stop_callback fromSession(sessionStop,
                                             [&operationStop] { operationStop.request_stop(); });
        const std::stop_callback fromCancel(command.stop,
                                            [&operationStop] { operationStop.request_stop(); });
        const TargetProfile& target = *command.target;
        const bool crossTarget = &target != m_config.target;
        const bool withImage =
            command.kind == OperationKind::Recover && target.recoverImage != RecoverImage::None;
        OperationReporter reporter(command.kind,
                                   plannedPhases(command.kind, crossTarget, withImage), clock(),
                                   m_config.timing.progressInterval,
                                   [this, id = command.id](const OperationProgress& progress) {
                                       emitProgress(id, progress);
                                   });
        if (command.kind == OperationKind::LteBundle) {
            reporter.step(1, 2);
        }
        openSink(); // the first operation of a probe-only session (review V12); no-op when open
        flashLog(
            std::format("Operation {} on {} started", operationKindId(command.kind), target.id));
        OperationResult result{.kind = command.kind,
                               .status = {},
                               .duration = {},
                               .chip = std::nullopt,
                               .data = {},
                               .checks = {}};
        if (m_suspended) {
            result.status = std::unexpected(notConnected("operation not run (suspended)"));
            finishOperation(command, reporter, std::move(result));
            return;
        }
        // Pause RTT without reporting a disconnect (ADR-0034).
        if (m_attached) {
            m_rtt->stop();
            m_attached = false;
        }
        OperationRun run{
            .command = command,
            .target = target,
            .crossTarget = crossTarget,
            .stop = operationStop.get_token(),
            .reporter = reporter,
            .context = WorkflowContext{
                .profile = m_config.profile,
                .target = &target,
                .programmer = nullptr,
                .progress = [&reporter](const firmware::ProgramProgress& p) { reporter.update(p); },
                .stop = operationStop.get_token(),
                .log = [this](const std::string& text) { flashLog(text); },
                .connectAs = {},
                .connectedDevice = {},
                .cpuHalted = false},
            .programmer = std::nullopt};
        Status outcome = executeOperation(run, result);
        // Cancelled while halted (e.g. during verify): let the CPU run again where possible -
        // also when the session is stopping (review R20).
        if (!outcome && outcome.error().code == ErrorCode::Cancelled && run.context.cpuHalted &&
            run.programmer && run.programmer->reset(ResetMode::Go)) {
            run.context.cpuHalted = false;
        }
        result.status = std::move(outcome);
        if (!sessionStop.stop_requested()) {
            restoreAfterOperation(crossTarget, run.context.cpuHalted, reporter, sessionStop);
        }
        finishOperation(command, reporter, std::move(result));
    }

    // ---- Diagnose (M4, plan/features/0007, ADR-0069) -------------------------------------------

    // The state of one Diagnose run (runDiagnose()).
    struct DiagnoseRun {
        OperationId id = 0;
        const TargetProfile* target = nullptr;
        std::stop_token stop;
        OperationResult* result = nullptr;
        OperationReporter* reporter = nullptr;
        bool failed = false;    // a step failed: the rest are Skipped
        bool cancelled = false; // stop requested: the rest are Skipped
        // The overall budget (review V10): a step that would start after `deadline` is Skipped
        // with diag.check_timeout, and so are the rest.
        Clock::TimePoint deadline;
        std::chrono::milliseconds budget{0};
        bool timedOut = false;
    };

    // Reports one finished step (event + result list), in order.
    void finishStep(DiagnoseRun& run, DiagnoseStep step) {
        if (step.status == diag::CheckStatus::Failed) {
            run.failed = true;
        }
        const int index = static_cast<int>(run.result->checks.size()) + 1;
        run.reporter->step(std::min(index + 1, static_cast<int>(diagnoseStepIds.size())),
                           static_cast<int>(diagnoseStepIds.size()));
        flashLog(std::format("Diagnose {}: {} {}", step.id, diag::checkStatusId(step.status),
                             step.error ? step.error->detail : std::string()));
        if (run.result->checks.size() < maxDiagnoseSteps) {
            run.result->checks.push_back(step);
        }
        emitEvent(DiagnoseStepEvent{.id = run.id, .step = std::move(step)});
    }

    static DiagnoseStep okStep(std::string_view id, const QString& text) {
        return DiagnoseStep{.id = std::string(id),
                            .status = diag::CheckStatus::Ok,
                            .result = text.toStdString(),
                            .error = std::nullopt};
    }

    static DiagnoseStep problemStep(std::string_view id, diag::CheckStatus status, Error error,
                                    std::string text = {}) {
        if (text.empty()) {
            text = error.message;
        }
        return DiagnoseStep{.id = std::string(id),
                            .status = status,
                            .result = std::move(text),
                            .error = std::move(error)};
    }

    static DiagnoseStep skippedStep(std::string_view id, const QString& text) {
        return DiagnoseStep{.id = std::string(id),
                            .status = diag::CheckStatus::Skipped,
                            .result = text.toStdString(),
                            .error = std::nullopt};
    }

    // Skips the step when an earlier one failed or a stop was requested. True = skipped.
    bool skipIfDone(DiagnoseRun& run, std::string_view id) {
        if (!run.cancelled && run.stop.stop_requested()) {
            run.cancelled = true;
        }
        if (run.cancelled) {
            finishStep(run,
                       skippedStep(id, QCoreApplication::translate("hio::device", "Cancelled")));
            return true;
        }
        if (run.failed) {
            finishStep(run,
                       skippedStep(id, QCoreApplication::translate("hio::device", "Not checked")));
            return true;
        }
        if (!run.timedOut && clock().now() > run.deadline) {
            run.timedOut = true;
        }
        if (run.timedOut) {
            finishStep(
                run, problemStep(id, diag::CheckStatus::Skipped,
                                 checkTimeoutError(*run.target, id,
                                                   std::format("not started: the Diagnose budget "
                                                               "of {} ms was used up",
                                                               run.budget.count())),
                                 QCoreApplication::translate(
                                     "hio::device", "Not checked: the time for the checks ran out")
                                     .toStdString()));
            return true;
        }
        return false;
    }

    // The translated title of step `id` of `target` ("Connection to the application MCU").
    [[nodiscard]] static std::string stepTitle(const TargetProfile& target, std::string_view id) {
        for (DiagnoseStepInfo& info : diagnoseSteps(target)) {
            if (info.id == id) {
                return std::move(info.title);
            }
        }
        return std::string(id);
    }

    // diag.check_timeout naming the step by its translated title (review V9).
    [[nodiscard]] static Error checkTimeoutError(const TargetProfile& target, std::string_view id,
                                                 std::string detail) {
        Error error =
            makeError(ErrorCode::DiagCheckTimeout, std::format("{}: {}", id, std::move(detail)));
        error.message =
            QCoreApplication::translate("hio::diag", "The check \"%1\" did not finish in time.")
                .arg(toQString(stepTitle(target, id)))
                .toStdString();
        return error;
    }

    // A step that ran longer than its budget and did not fail becomes a Warning (plan 0007, Risks:
    // a blocking vendor call cannot be interrupted).
    void finishTimed(DiagnoseRun& run, DiagnoseStep step, Clock::TimePoint started,
                     std::chrono::milliseconds budget) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(clock().now() - started);
        if (elapsed > budget && step.status == diag::CheckStatus::Ok) {
            Error error =
                checkTimeoutError(*run.target, step.id,
                                  std::format("{} ms > {} ms", elapsed.count(), budget.count()));
            step = problemStep(step.id, diag::CheckStatus::Warning, std::move(error), step.result);
        }
        finishStep(run, std::move(step));
    }

    // Step budgets (sum = diagnoseBudget; the rtt step uses its own deadline + 500 ms).
    static constexpr std::chrono::milliseconds probeStepBudget{4000};
    static constexpr std::chrono::milliseconds powerStepBudget{1000};
    static constexpr std::chrono::milliseconds connectStepBudget{4000};
    static constexpr std::chrono::milliseconds identifyStepBudget{2000};
    static constexpr std::chrono::milliseconds buffersStepBudget{500};

    // Step 1: the library, the probe and its firmware.
    void diagnoseProbe(DiagnoseRun& run) {
        const Clock::TimePoint started = clock().now();
        Status opened = ensureApi();
        if (opened && !m_probe->isOpen()) {
            opened = openProbe(run.stop);
        }
        if (!opened) {
            if (opened.error().code == ErrorCode::Cancelled) {
                run.cancelled = true;
                finishStep(run, skippedStep("probe", QCoreApplication::translate("hio::device",
                                                                                 "Cancelled")));
                return;
            }
            finishStep(run,
                       problemStep("probe", diag::CheckStatus::Failed, std::move(opened).error()));
            return;
        }
        const jlink::ProbeInfo& info = m_probe->info();
        const QString text =
            info.product.empty()
                ? QString::number(info.serialNumber)
                : QStringLiteral("%1 · %2").arg(toQString(info.product)).arg(info.serialNumber);
        // Unknown is not a failure: nothing is known to be wrong (review R11; HIL 2026-10-16).
        const jlink::FirmwareStatus firmware = m_probe->firmwareStatus();
        if (firmware == jlink::FirmwareStatus::Outdated ||
            firmware == jlink::FirmwareStatus::Newer) {
            Error error =
                makeError(ErrorCode::ProbeFirmwareOutdated,
                          std::format("J-Link {} firmware: {}", info.serialNumber, info.firmware));
            if (firmware == jlink::FirmwareStatus::Newer) {
                error.message =
                    QCoreApplication::translate(
                        "hio::device", "The J-Link firmware is newer than the J-Link library.")
                        .toStdString();
            }
            finishTimed(run,
                        problemStep("probe", diag::CheckStatus::Warning, std::move(error),
                                    text.toStdString()),
                        started, probeStepBudget);
            return;
        }
        finishTimed(run, okStep("probe", text), started, probeStepBudget);
    }

    [[nodiscard]] static QString voltsText(int millivolts) {
        return QStringLiteral("%1 V").arg(static_cast<double>(millivolts) / 1000.0, 0, 'f', 2);
    }

    [[nodiscard]] static Error noPowerError(int millivolts) {
        Error error = makeError(ErrorCode::TargetNoPower, std::format("VTref {} mV", millivolts));
        error.message =
            QCoreApplication::translate("hio::device", "The device has no power (%1 mV).")
                .arg(millivolts)
                .toStdString();
        return error;
    }

    // Steps 2 and 3: the target voltage, then one connect attempt. 0 mV before any healthy reading
    // may be a probe that cannot measure VTref (review R21/R33): the power step then waits for the
    // connect: a target that connects is powered.
    void diagnosePowerAndConnect(DiagnoseRun& run) {
        const TargetProfile& target = *run.target;
        if (skipIfDone(run, "power")) {
            (void)skipIfDone(run, "connect");
            return;
        }
        Clock::TimePoint started = clock().now();
        const auto status = m_probe->hardwareStatus();
        if (!status) {
            finishStep(run, problemStep("power", diag::CheckStatus::Failed, status.error()));
            (void)skipIfDone(run, "connect");
            return;
        }
        const int millivolts = status->targetMillivolts;
        rememberVoltage(*status);
        bool powerDeferred = false;
        if (millivolts >= target.minTargetMillivolts) {
            m_seenPower = true;
            finishTimed(run, okStep("power", voltsText(millivolts)), started, powerStepBudget);
        } else if (millivolts > 0 || m_seenPower) {
            finishStep(run,
                       problemStep("power", diag::CheckStatus::Failed, noPowerError(millivolts)));
        } else {
            powerDeferred = true;
        }

        if (!powerDeferred && skipIfDone(run, "connect")) {
            return;
        }
        started = clock().now();
        Status connected;
        bool already = m_probeTarget == &target && m_api->isConnected();
        if (!already) {
            m_probe->setSpeed(m_config.speedKhz);
            connected = m_probe->selectSwd();
            if (connected) {
                connected = connectDevice(target, target.jlinkDevice, run.stop, 1);
            }
        }
        finishConnect(run, std::move(connected), powerDeferred, started);
    }

    // Step 3 after the connect attempt (and step 2 when its result waited for the connect).
    void finishConnect(DiagnoseRun& run, Status connected, bool powerDeferred,
                       Clock::TimePoint started) {
        if (connected && powerDeferred) {
            finishStep(run, okStep("power", QCoreApplication::translate(
                                                "hio::device", "Not measured by this J-Link")));
        }
        if (!connected && connected.error().code == ErrorCode::Cancelled) {
            run.cancelled = true;
            if (powerDeferred) {
                finishStep(run, skippedStep("power", QCoreApplication::translate("hio::device",
                                                                                 "Cancelled")));
            }
            finishStep(run, skippedStep("connect",
                                        QCoreApplication::translate("hio::device", "Cancelled")));
            return;
        }
        if (!connected && powerDeferred) {
            finishStep(run, problemStep("power", diag::CheckStatus::Failed, noPowerError(0)));
            (void)skipIfDone(run, "connect");
            return;
        }
        if (!connected) {
            // A target that does not connect may be read-back protected, or the cable may be on the
            // other MCU's connector: its CTRL-AP may still answer (reviews R2, R15). The identify
            // step then reports what was found.
            Error error = std::move(connected).error();
            m_diagnoseIdentity = identifyByCtrlAp(run);
            if (m_diagnoseIdentity && (m_diagnoseIdentity->code == ErrorCode::TargetProtected ||
                                       m_diagnoseIdentity->code == ErrorCode::TargetWrongFamily)) {
                finishStep(run, problemStep("connect", diag::CheckStatus::Warning, std::move(error),
                                            QCoreApplication::translate(
                                                "hio::device", "Only the debug port answers")
                                                .toStdString()));
                return;
            }
            m_diagnoseIdentity.reset();
            finishStep(run, problemStep("connect", diag::CheckStatus::Failed, std::move(error)));
            return;
        }
        const QString text =
            m_config.speedKhz
                ? QCoreApplication::translate("hio::device", "Connected (SWD %1 kHz)")
                      .arg(*m_config.speedKhz)
                : QCoreApplication::translate("hio::device", "Connected (SWD, automatic speed)");
        finishTimed(run, okStep("connect", text), started, connectStepBudget);
    }

    // The CTRL-AP answer for a target that did not connect: target.protected or
    // target.wrong_family, nullopt otherwise.
    [[nodiscard]] std::optional<Error> identifyByCtrlAp(DiagnoseRun& run) {
        jlink::JLinkProgrammer programmer(*m_api, *m_probe, clock(),
                                          programmerConfigFor(*run.target));
        WorkflowContext context = diagnoseContext(run, programmer);
        auto chip = identifyTarget(context, false);
        if (chip) {
            return std::nullopt;
        }
        return std::move(chip).error();
    }

    [[nodiscard]] WorkflowContext diagnoseContext(DiagnoseRun& run,
                                                  jlink::IProgrammer& programmer) {
        OperationReporter& reporter = *run.reporter;
        return WorkflowContext{
            .profile = m_config.profile,
            .target = run.target,
            .programmer = &programmer,
            .progress = [&reporter](const firmware::ProgramProgress& p) { reporter.update(p); },
            .stop = run.stop,
            .log = [this](const std::string& text) { flashLog(text); },
            .connectAs =
                [this, &run](std::string_view device) {
                    return connectDevice(*run.target, device, run.stop, 1);
                },
            .connectedDevice = m_connectedDevice,
            .cpuHalted = false};
    }

    // Step 4: FICR part and family, protection. Returns true when the MCU is protected.
    bool diagnoseIdentify(DiagnoseRun& run) {
        if (m_diagnoseIdentity) {
            // From the CTRL-AP after a failed connect.
            Error error = std::move(*m_diagnoseIdentity);
            m_diagnoseIdentity.reset();
            const bool isProtected = error.code == ErrorCode::TargetProtected;
            if (skipIfDone(run, "identify")) {
                return isProtected;
            }
            finishStep(run, problemStep("identify",
                                        isProtected ? diag::CheckStatus::Warning
                                                    : diag::CheckStatus::Failed,
                                        std::move(error)));
            return isProtected;
        }
        if (skipIfDone(run, "identify")) {
            return false;
        }
        const Clock::TimePoint started = clock().now();
        jlink::JLinkProgrammer programmer(*m_api, *m_probe, clock(),
                                          programmerConfigFor(*run.target));
        WorkflowContext context = diagnoseContext(run, programmer);
        auto chip = identifyTarget(context, false);
        m_connectedDevice = context.connectedDevice;
        if (!chip) {
            Error error = std::move(chip).error();
            if (error.code == ErrorCode::Cancelled) {
                run.cancelled = true;
                finishStep(run, skippedStep("identify", QCoreApplication::translate("hio::device",
                                                                                    "Cancelled")));
                return false;
            }
            const bool isProtected = error.code == ErrorCode::TargetProtected;
            finishStep(run, problemStep("identify",
                                        isProtected ? diag::CheckStatus::Warning
                                                    : diag::CheckStatus::Failed,
                                        std::move(error)));
            return isProtected;
        }
        run.result->chip = *chip;
        finishTimed(
            run,
            okStep("identify", QCoreApplication::translate("hio::device", "%1, not protected")
                                   .arg(toQString(chip->name))),
            started, identifyStepBudget);
        return false;
    }

    // Both RTT steps Skipped with `text` (or Cancelled / Not checked after a stop or a failure).
    void skipRttSteps(DiagnoseRun& run, const QString& text) {
        for (const std::string_view id :
             {std::string_view("rtt"), std::string_view("rtt-buffers")}) {
            if (!skipIfDone(run, id)) {
                finishStep(run, skippedStep(id, text));
            }
        }
    }

    // Steps 5 and 6: the RTT control block and the profile's buffer names. On the console target
    // of an attached session the running attachment is checked (no re-attach).
    void diagnoseRtt(DiagnoseRun& run, bool isProtected, bool keepRtt) {
        const TargetProfile& target = *run.target;
        if (target.rttTerminal.empty()) {
            skipRttSteps(run,
                         QCoreApplication::translate("hio::device", "This MCU has no console"));
            return;
        }
        if (isProtected) {
            skipRttSteps(run, QCoreApplication::translate("hio::device",
                                                          "Not checked: the MCU is protected"));
            return;
        }
        if (skipIfDone(run, "rtt")) {
            (void)skipIfDone(run, "rtt-buffers");
            return;
        }
        Clock::TimePoint started = clock().now();
        jlink::RttLayout layout;
        jlink::RttTiming timing = m_config.timing.rtt;
        // ADR-0110: the budget scales with a fixed low speed like the session's
        // attach deadline.
        timing.attachDeadline =
            jlink::rttAttachDeadline(m_config.speedKhz, m_config.timing.diagnoseRttBudget);
        timing.loggerWait = std::min(timing.loggerWait, m_config.timing.diagnoseRttBudget / 3);
        const std::chrono::milliseconds rttBudget =
            timing.attachDeadline + timing.loggerWait + std::chrono::milliseconds(500);
        std::optional<Error> buffersError;
        if (keepRtt) {
            layout = m_rtt->layout();
        } else {
            jlink::RttChannel channel(*m_api, clock(), timing, m_config.speedKhz);
            const jlink::RttNames names{.terminal = std::string(target.rttTerminal),
                                        .logger = std::string(target.rttLogger)};
            auto attached = channel.attach(names, run.stop);
            channel.stop();
            if (!attached) {
                Error error = std::move(attached).error();
                if (error.code == ErrorCode::Cancelled) {
                    run.cancelled = true;
                    finishStep(run, skippedStep("rtt", QCoreApplication::translate("hio::device",
                                                                                   "Cancelled")));
                    (void)skipIfDone(run, "rtt-buffers");
                    return;
                }
                if (error.code != ErrorCode::TargetRttBufferNotFound) {
                    finishStep(run,
                               problemStep("rtt", diag::CheckStatus::Failed, std::move(error)));
                    (void)skipIfDone(run, "rtt-buffers");
                    return;
                }
                buffersError = std::move(error); // the control block is there
            } else {
                layout = *attached;
            }
        }
        finishTimed(run, okStep("rtt", QCoreApplication::translate("hio::device", "Found")),
                    started, rttBudget);

        if (skipIfDone(run, "rtt-buffers")) {
            return;
        }
        started = clock().now();
        if (buffersError) {
            finishStep(run, problemStep("rtt-buffers", diag::CheckStatus::Failed,
                                        std::move(*buffersError)));
            return;
        }
        QStringList found{toQString(target.rttTerminal)};
        if (!target.rttLogger.empty()) {
            if (!layout.loggerUp) {
                Error error = makeError(ErrorCode::TargetRttBufferNotFound,
                                        std::format("logger buffer \"{}\"", target.rttLogger));
                error.message =
                    QCoreApplication::translate(
                        "hio::device", "RTT buffer \"%1\" not found; the device log is not shown.")
                        .arg(toQString(target.rttLogger))
                        .toStdString();
                finishStep(run,
                           problemStep("rtt-buffers", diag::CheckStatus::Warning, std::move(error),
                                       found.join(QStringLiteral(", ")).toStdString()));
                return;
            }
            found.push_back(toQString(target.rttLogger));
        }
        finishTimed(run, okStep("rtt-buffers", found.join(QStringLiteral(", "))), started,
                    buffersStepBudget);
    }

    void runDiagnose(const OperationCommand& command, const std::stop_token& sessionStop) {
        std::stop_source operationStop;
        const std::stop_callback fromSession(sessionStop,
                                             [&operationStop] { operationStop.request_stop(); });
        const std::stop_callback fromCancel(command.stop,
                                            [&operationStop] { operationStop.request_stop(); });
        const TargetProfile& target = *command.target;
        const bool consoleTarget = &target == m_config.target;
        // The console target of an attached session: RTT keeps running (ADR-0069).
        const bool keepRtt = consoleTarget && m_attached;
        // Another target of a normal session: switch and restore (ADR-0057). A probe-only session
        // has no target to restore.
        const bool crossTarget = !consoleTarget && !m_probeOnly;
        OperationReporter reporter(command.kind, plannedPhases(command.kind, crossTarget), clock(),
                                   m_config.timing.progressInterval,
                                   [this, id = command.id](const OperationProgress& progress) {
                                       emitProgress(id, progress);
                                   });
        reporter.step(1, static_cast<int>(diagnoseStepIds.size()));
        flashLog(std::format("Operation diagnose on {} started", target.id));
        OperationResult result{.kind = command.kind,
                               .status = {},
                               .duration = {},
                               .chip = std::nullopt,
                               .data = {},
                               .checks = {}};
        if (m_suspended) {
            result.status = std::unexpected(notConnected("diagnose not run (suspended)"));
            finishOperation(command, reporter, std::move(result));
            return;
        }
        if (m_attached && !keepRtt) {
            m_rtt->stop(); // pause RTT without reporting a disconnect (ADR-0034)
            m_attached = false;
        }
        if (crossTarget) {
            reporter.phase(ProgramPhase::SwitchingTarget);
        }
        reporter.phase(ProgramPhase::Connecting);
        // The overall budget (review V10): diagnoseBudget plus what a fixed low speed adds to the
        // rtt step's search (ADR-0110), so that the rtt step's own budget always fits.
        const std::chrono::milliseconds budget =
            m_config.timing.diagnoseBudget +
            (jlink::rttAttachDeadline(m_config.speedKhz, m_config.timing.diagnoseRttBudget) -
             m_config.timing.diagnoseRttBudget);
        DiagnoseRun run{.id = command.id,
                        .target = &target,
                        .stop = operationStop.get_token(),
                        .result = &result,
                        .reporter = &reporter,
                        .deadline = clock().now() + budget,
                        .budget = budget};
        m_diagnoseIdentity.reset();
        diagnoseProbe(run);
        if (!run.failed && !run.cancelled) {
            diagnosePowerAndConnect(run);
        } else {
            for (const std::string_view id : {"power", "connect"}) {
                (void)skipIfDone(run, id);
            }
        }
        const bool isProtected = diagnoseIdentify(run);
        diagnoseRtt(run, isProtected, keepRtt);
        m_diagnoseIdentity.reset();
        if (run.cancelled) {
            result.status = fail(ErrorCode::Cancelled);
        }
        if (!sessionStop.stop_requested() && !keepRtt) {
            if (m_probeOnly) {
                if (m_probe && m_probe->isOpen()) {
                    report(ConnectionState::ProbeOpen);
                }
            } else {
                restoreAfterOperation(crossTarget, false, reporter, sessionStop);
            }
        }
        finishOperation(command, reporter, std::move(result));
    }

    void finishOperation(const OperationCommand& command, OperationReporter& reporter,
                         OperationResult result) {
        reporter.flush();
        result.duration = reporter.elapsed();
        if (result.status) {
            flashLog(std::format("Operation {} finished in {} ms", operationKindId(command.kind),
                                 result.duration.count()));
        } else {
            flashLog(std::format("Operation {} failed: {} ({})", operationKindId(command.kind),
                                 result.status.error().message, result.status.error().detail));
        }
        flushSink();
        emitEvent(OperationFinishedEvent{.id = command.id, .result = std::move(result)});
    }

    void handle(Command command, const std::stop_token& stop) {
        std::visit(Overloaded{
                       [&](SendLineCommand& send) { sendLine(std::move(send.line), stop); },
                       [&](const OperationCommand& operation) {
                           if (operation.kind == OperationKind::Diagnose) {
                               runDiagnose(operation, stop);
                           } else {
                               runOperation(operation, stop);
                           }
                       },
                       [&](const StartConnectionCommand& start) {
                           // ADR-0069: promote the probe-only session; the probe and its lock
                           // stay held. Only once (review V5): a promoted session ignores it.
                           if (!m_probeOnly) {
                               qCInfo(log::lcDevice) << "Start connection ignored: not probe-only";
                               return;
                           }
                           m_probeOnly = false;
                           m_config.attachRtt = start.attachRtt;
                           m_fatal = false;
                           m_suspended = false;
                           m_halted = false;
                           connect(stop, true);
                       },
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
    ModemUpdaterFactory m_modemFactory;
    std::optional<ProbeLock> m_lock;
    std::reference_wrapper<BoundedQueue<Command>> m_commands;
    std::reference_wrapper<BoundedQueue<WorkerEvent>> m_events;
    std::reference_wrapper<const std::function<void()>> m_wake;
    std::reference_wrapper<jlink::SystemClock> m_clock{jlink::SystemClock::instance()};

    std::unique_ptr<jlink::IJLinkApi> m_api;
    std::unique_ptr<jlink::JLinkProbe> m_probe; // uses *m_api; destroyed before it
    std::unique_ptr<jlink::RttChannel> m_rtt;   // uses *m_api; destroyed before it
    std::optional<std::uint32_t> m_serial;
    const TargetProfile* m_probeTarget = nullptr; // the target the probe is connected to now
    std::string m_connectedDevice;                // with this J-Link device name
    std::unique_ptr<ConsoleFileSink> m_sink;
    std::filesystem::path m_reportedConsoleFile; // the last ConsoleFileChanged
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
    std::optional<int> m_lastMillivolts; // ADR-0091
    bool m_powered = true;               // false after a power loss (or an unanswered status read)
    bool m_firmwareChecked = false;
    bool m_loggerWarned = false;
    bool m_resetDone = false;
    bool m_sinkOpened = false;
    bool m_probeOnly = false; // ADR-0069: connectTarget = false until startConnection()
    // Diagnose: what the CTRL-AP said after a failed connect (consumed by the identify step).
    std::optional<Error> m_diagnoseIdentity;
};

} // namespace

void runWorker(const std::stop_token& stop, WorkerConfig config, ApiFactory apiFactory,
               ModemUpdaterFactory modemUpdaterFactory, std::optional<ProbeLock> lock,
               BoundedQueue<Command>& commands, BoundedQueue<WorkerEvent>& events,
               const std::function<void()>& wake, std::atomic<bool>& failed) {
    try {
        SessionWorker worker(std::move(config), std::move(apiFactory),
                             std::move(modemUpdaterFactory), std::move(lock), commands, events,
                             wake);
        worker.run(stop);
    } catch (...) {
        // Remembered before the event, which a full queue could lose (review R14).
        failed.store(true);
        // No exception crosses the thread boundary (ADR-0007): report it as a fatal error.
        if (events.push(ConnectionEvent{.state = ConnectionState::Stopped,
                                        .error = makeError(ErrorCode::InternalUnexpected,
                                                           "exception in the session worker"),
                                        .targetMillivolts = std::nullopt})) {
            wake();
        }
    }
}

} // namespace hio::device::detail
