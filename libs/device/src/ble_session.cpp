#include "hio/device/ble_session.h"

#include "ble_session_internal.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/core/version.h"
#include "hio/device/ble_lock.h"
#include "hio/device/console_file_sink.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/console_session.h"
#include "hio/device/line_assembler.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/device/session_key.h"
#include "hio/device/session_registry.h"
#include "hio/hw/ble/ble_host.h"
#include "hio/hw/ble/nus_link.h"
#include "hio/hw/ble/types.h"

#include <QCoreApplication>
#include <QList>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtCore/qnamespace.h>
#include <QtLogging>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <qtmetamacros.h>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace hio::device {

namespace {

template <class... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
};

using Clock = std::chrono::steady_clock;

Error invalidOption(const QString& message, std::string detail) {
    Error error = makeError(ErrorCode::UsageInvalidArgument, std::move(detail));
    error.message = message.toStdString();
    return error;
}

} // namespace

namespace detail {

bool isFatalBleError(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::BleAdapterNotFound:
    case ErrorCode::BleAdapterOff:
    case ErrorCode::BlePermissionDenied:
    case ErrorCode::BleStackUnavailable:
    case ErrorCode::BleServiceNotFound:
    case ErrorCode::BleNotPaired:
    case ErrorCode::BlePairingFailed:
    case ErrorCode::BleInUseByOtherProcess:
    case ErrorCode::InternalSessionLimit:
        return true;
    default:
        return false;
    }
}

// ---- BleConsoleHandler (BLE thread)
// ----------------------------------------------------------------

BleConsoleHandler::BleConsoleHandler(BleHandlerConfig config,
                                     std::shared_ptr<BleEventChannel> channel)
    : m_config(std::move(config)), m_channel(std::move(channel)),
      m_shell(Channel::Shell, m_config.target->hashPrefixIsLog),
      m_prompt(m_config.profile->ble ? std::string(m_config.profile->ble->shellPrompt)
                                     : std::string()) {}

BleConsoleHandler::~BleConsoleHandler() { onClosed(); }

void BleConsoleHandler::emitEvent(BleSessionEvent event) {
    static_cast<void>(m_channel->push(std::move(event))); // fails only after detach (closed)
}

void BleConsoleHandler::warn(Error error) {
    qCWarning(log::lcBle).noquote()
        << "Warning:" << toQString(error.message) << toQString(error.detail);
    emitEvent(Warning{.error = std::move(error)});
}

void BleConsoleHandler::emitLine(ConsoleLine line) {
    if (m_sink) {
        sinkResult(m_sink->write(line));
    }
    emitEvent(std::move(line));
}

void BleConsoleHandler::report(ConnectionState state, std::optional<Error> error, bool force) {
    const std::optional<ErrorCode> code =
        error ? std::optional<ErrorCode>(error->code) : std::nullopt;
    if (m_reported == state && m_reportedCode == code && !force) {
        return;
    }
    m_reported = state;
    m_reportedCode = code;
    std::string text(connectionStateId(state));
    if (error) {
        text += ": " + error->message;
        qCInfo(log::lcBle).noquote()
            << "Connection" << toQString(text) << "-" << toQString(error->detail);
    } else {
        qCInfo(log::lcBle).noquote() << "Connection" << toQString(text);
    }
    if (m_sink) {
        sinkResult(m_sink->writeConnection(text, std::chrono::system_clock::now()));
    }
    emitEvent(ConnectionEvent{.state = state,
                              .error = std::move(error),
                              .targetMillivolts = std::nullopt,
                              .transport = Transport::Ble,
                              .mtu = state == ConnectionState::Connected
                                         ? std::optional<std::uint16_t>(m_mtu)
                                         : std::nullopt});
}

void BleConsoleHandler::sinkResult(const Status& status) {
    if (!status && m_sink) {
        m_sink.reset(); // the session continues without the file
        reportConsoleFile();
        warn(status.error());
        return;
    }
    if (m_sink) {
        reportConsoleFile();
        if (auto warning = m_sink->takeWarning()) {
            warn(std::move(*warning));
        }
    }
}

void BleConsoleHandler::reportConsoleFile() {
    static const std::filesystem::path none;
    const std::filesystem::path& current = m_sink ? m_sink->currentFile() : none;
    if (current.native() == m_reportedConsoleFile.native()) {
        return;
    }
    m_reportedConsoleFile = current;
    emitEvent(BleConsoleFileChanged{.file = current});
}

void BleConsoleHandler::openSink() {
    if (m_sinkOpened || !m_config.consoleFile) {
        return;
    }
    m_sinkOpened = true;
    // HIL H7: the name in parentheses only when it is known and is not the id itself (-a).
    const bool named =
        !m_config.deviceName.empty() && !ble::sameDeviceId(m_config.deviceName, m_config.deviceId);
    const std::string device =
        named ? std::format("{} ({})", m_config.deviceId, m_config.deviceName) : m_config.deviceId;
    const std::string header =
        std::format("Console: {}, Bluetooth {}, hio {}", m_config.profile->displayName, device,
                    buildInfo().version);
    auto sink = ConsoleFileSink::open(*m_config.consoleFile, BleKey{.id = m_config.deviceId},
                                      header, std::chrono::system_clock::now());
    if (!sink) {
        warn(std::move(sink).error());
        return;
    }
    m_sink = std::move(*sink);
    sinkResult({});
}

void BleConsoleHandler::onConnecting(bool automatic) {
    openSink();
    ++m_attempts;
    m_shell.reset(); // a new link starts from scratch
    m_afterPrompt = false;
    m_afterWrite = false;
    if (!automatic) {
        m_fatal = false;
        report(ConnectionState::Connecting);
    }
}

void BleConsoleHandler::onPairingPrompt() { emitEvent(BlePairingPrompt{}); }

void BleConsoleHandler::onReady(ble::LinkReady ready) {
    m_mtu = ready.mtu;
    m_everConnected = true;
    report(ConnectionState::Connected, std::nullopt, true);
    emitEvent(BleAttemptEnded{});
}

void BleConsoleHandler::onData(std::span<const std::byte> data) {
    const auto now = std::chrono::system_clock::now();
    for (ConsoleLine& line : m_shell.feed(data, now)) {
        // ADR-0082: NUS carries the shell; a line with a Zephyr level tag is a log line (the
        // '#' rule is the assembler's, hashPrefixIsLog of the console target).
        if (line.channel == Channel::Shell) {
            if (const LogLevel level = parseZephyrLevel(line.text); level != LogLevel::None) {
                line.channel = Channel::Log;
                line.level = level;
            } else if (isHexdumpLine(line.text)) {
                // ADR-0171: the continuation lines of a log hexdump are log lines too.
                line.channel = Channel::Log;
                line.level = LogLevel::Hexdump;
            }
        }
        if (line.channel == Channel::Shell) {
            // HIL H5: the shell (no echo) answers a command with a new line first; that one empty
            // line - the first shell line after a written line, or after a prompt that was not
            // emitted - is not output. Later empty lines are.
            const bool afterWrite = std::exchange(m_afterWrite, false);
            const bool afterPrompt = std::exchange(m_afterPrompt, false);
            if ((afterWrite || afterPrompt) && line.text.empty()) {
                continue;
            }
        }
        emitLine(std::move(line));
    }
    // ADR-0082: the prompt is not a line; the session reports it instead.
    if (isPrompt(m_shell.partial(), m_prompt)) {
        m_shell.reset();
        m_afterPrompt = true;
        emitEvent(BlePromptSeen{});
    }
}

void BleConsoleHandler::onLineWritten(std::string line) {
    ConsoleLine echo;
    echo.channel = Channel::Shell;
    echo.dir = Direction::In;
    echo.time = std::chrono::system_clock::now();
    echo.text = std::move(line);
    m_afterWrite = true;       // H5: the shell's new line after Enter follows
    emitLine(std::move(echo)); // only after it was written (ADR-0082)
}

void BleConsoleHandler::onLineRejected(std::string /*line*/, Error error) {
    emitEvent(BleLineRejected{});
    if (error.code == ErrorCode::TargetNotConnected && m_config.autoReconnect && !m_fatal) {
        // Only when the session will really reconnect by itself (as DeviceSession, review R35).
        error.suggestion =
            QCoreApplication::translate("hio::device", "Wait for the console to reconnect.")
                .toStdString();
    }
    warn(std::move(error));
}

void BleConsoleHandler::onDown(Error error) {
    const bool initial = !m_everConnected && m_attempts <= 1;
    if (error.code == ErrorCode::BleDisconnected || error.code == ErrorCode::BleConnectFailed ||
        error.code == ErrorCode::BleDeviceNotFound || error.code == ErrorCode::BlePairingFailed) {
        // The device name in the message (the default text has no placeholder).
        const std::string name =
            m_config.deviceName.empty() ? m_config.deviceId : m_config.deviceName;
        const QString who = toQString(name);
        if (error.code == ErrorCode::BleDisconnected) {
            error.message = QCoreApplication::translate("hio::device",
                                                        "The Bluetooth connection to %1 was lost.")
                                .arg(who)
                                .toStdString();
        } else if (error.code == ErrorCode::BleConnectFailed) {
            error.message = QCoreApplication::translate("hio::device",
                                                        "Could not connect to %1 over Bluetooth.")
                                .arg(who)
                                .toStdString();
        } else if (error.code == ErrorCode::BlePairingFailed) {
            error.message = QCoreApplication::translate("hio::device", "Pairing with %1 failed.")
                                .arg(who)
                                .toStdString();
        }
    }
    emitEvent(BleAttemptEnded{});
    if (isFatalBleError(error.code) || (initial && !m_config.autoReconnect)) {
        m_fatal = true;
        report(ConnectionState::Stopped, std::move(error), true);
        return;
    }
    report(ConnectionState::Disconnected, std::move(error));
}

void BleConsoleHandler::onPairingFinished(Status status, bool alreadyPaired) {
    if (status) {
        qCInfo(log::lcBle) << "Paired" << (alreadyPaired ? "(already)" : "");
    }
    emitEvent(BlePairingFinished{.status = std::move(status), .alreadyPaired = alreadyPaired});
}

void BleConsoleHandler::onFlush() {
    if (m_sink) {
        sinkResult(m_sink->flush());
    }
}

void BleConsoleHandler::onClosed() {
    if (m_closed) {
        return;
    }
    m_closed = true;
    if (m_sink) {
        sinkResult(m_sink->writeConnection(std::string(connectionStateId(ConnectionState::Stopped)),
                                           std::chrono::system_clock::now()));
        onFlush();
    }
    m_sink.reset();
}

// ---- open
// --------------------------------------------------------------------------------------------

Result<std::unique_ptr<BleSession>> openBleSession(SessionRegistry& registry, ble::BleHost& host,
                                                   BleSessionOptions options,
                                                   BleSessionTiming timing) {
    return BleSessionInternals::open(registry, host, std::move(options), timing);
}

Result<std::unique_ptr<BleSession>> BleSessionInternals::open(SessionRegistry& registry,
                                                              ble::BleHost& host,
                                                              BleSessionOptions options,
                                                              BleSessionTiming timing) {
    // 1. options
    if (options.profile == nullptr) {
        return std::unexpected(invalidOption(
            QCoreApplication::translate("hio::device", "No device profile was given."), {}));
    }
    if (!options.profile->ble) {
        return std::unexpected(invalidOption(
            QCoreApplication::translate("hio::device", "The device %1 has no Bluetooth console.")
                .arg(toQString(options.profile->displayName)),
            std::string(options.profile->id)));
    }
    const BleProfile& ble = *options.profile->ble;
    const TargetProfile* target = findTarget(*options.profile, ble.consoleTargetId);
    if (target == nullptr) {
        return std::unexpected(invalidOption(
            QCoreApplication::translate("hio::device", "The device %1 has no target \"%2\".")
                .arg(toQString(options.profile->displayName), toQString(ble.consoleTargetId)),
            {}));
    }
    // Review B6: one form per device (lock file, registry key, console file name).
    options.deviceId = ble::canonicalDeviceId(options.deviceId);
    if (options.deviceId.empty()) {
        return std::unexpected(invalidOption(
            QCoreApplication::translate("hio::device", "No Bluetooth device was given."), {}));
    }
    // 2. the registry slot, 3. the cross-process lock (ADR-0083)
    if (auto available =
            registry.checkAvailable(std::optional<SessionKey>(BleKey{.id = options.deviceId}));
        !available) {
        return std::unexpected(std::move(available).error());
    }
    auto lock = BleLock::acquire(options.deviceId, registry.options().lockDirectory);
    if (!lock) {
        return std::unexpected(std::move(lock).error());
    }

    auto internals = std::make_unique<BleSessionInternals>();
    internals->profile = options.profile;
    internals->target = target;
    internals->deviceId = options.deviceId;
    internals->deviceName = options.deviceName;
    internals->autoReconnect = options.autoReconnect;
    internals->log = options.consoleLog ? options.consoleLog : std::make_shared<ConsoleLog>();
    internals->lock = std::move(*lock);
    internals->client = host.newClientId();
    internals->timing = timing;

    BleHandlerConfig config{.profile = options.profile,
                            .target = target,
                            .deviceId = options.deviceId,
                            .deviceName = options.deviceName,
                            .autoReconnect = options.autoReconnect,
                            .consoleFile = std::nullopt};
    if (!options.consoleFile || !options.consoleFile->empty()) {
        config.consoleFile = ConsoleFileConfig{.explicitFile = options.consoleFile,
                                               .directory = registry.options().consoleDirectory,
                                               .profileId = std::string(options.profile->id),
                                               .targetId = std::string(target->id),
                                               .lockDirectory = registry.options().lockDirectory,
                                               .consoleFileDir = options.consoleFileDir};
    }

    auto session =
        std::make_unique<BleSession>(BleSession::Passkey{}, registry, host, std::move(internals));
    BleSession* facade = session.get();
    BleSessionInternals& state = *facade->m_internals;
    // The wake functions run on the BLE thread; detach() (closeNow) stops them before the facade
    // goes away.
    state.events = std::make_shared<BleEventChannel>(
        BleSession::eventQueueCapacity, &isDroppableBleEvent, [facade] { facade->wakeUp(); });
    state.scan = ble::BleHost::subscribe([facade] { facade->wakeUp(); });
    registry.add(facade);

    const ble::NusUuids uuids{.service = std::string(ble.serviceUuid),
                              .rx = std::string(ble.rxUuid),
                              .tx = std::string(ble.txUuid)};
    if (Status posted = host.post(ble::OpenLink{
            .client = state.client,
            .device = options.deviceId,
            .uuids = uuids,
            .handler = std::make_unique<BleConsoleHandler>(std::move(config), state.events),
            .timing = timing.link});
        !posted) {
        session->close();
        return std::unexpected(std::move(posted).error());
    }
    qCInfo(log::lcBle).noquote() << QStringLiteral("Session opened: %1 over Bluetooth, %2 %3")
                                        .arg(toQString(options.profile->id),
                                             toQString(options.deviceId),
                                             toQString(options.deviceName));
    return session;
}

} // namespace detail

// ---- BleSession
// -----------------------------------------------------------------------------------

Result<std::unique_ptr<BleSession>> BleSession::open(SessionRegistry& registry, ble::BleHost& host,
                                                     BleSessionOptions options) {
    return detail::openBleSession(registry, host, std::move(options), detail::BleSessionTiming{});
}

BleSession::BleSession(Passkey /*key*/, SessionRegistry& registry, ble::BleHost& host,
                       std::unique_ptr<detail::BleSessionInternals> internals)
    : m_registry(&registry), m_host(&host), m_internals(std::move(internals)) {
    m_connection.transport = Transport::Ble;
    m_retryTimer.setInterval(std::chrono::milliseconds(100));
    QObject::connect(&m_retryTimer, &QTimer::timeout, this, [this] { retryTick(); });
}

BleSession::~BleSession() { closeNow(); }

void BleSession::closeSession() { closeNow(); }

void BleSession::closeNow() {
    if (m_closed) {
        return;
    }
    m_closed = true;
    m_retryTimer.stop();
    detail::BleSessionInternals& state = *m_internals;
    if (state.events) {
        state.events->detach();
    }
    if (state.scan) {
        state.scan->detach();
        if (m_scanning) {
            stopBackgroundScan();
        }
    }
    // Non-blocking (plan 0011 decisions, 2026-10-19): the BLE thread disconnects the link, writes
    // the last console file line and closes the file, then destroys the handler. Commands of one
    // host run in order, so a new session of the same device (OpenLink posted after this) is
    // connected and opens its file only after this link and file were closed.
    if (Status posted = m_host->post(ble::CloseLink{.client = state.client, .done = nullptr});
        !posted) {
        // A full command queue (transient) or a stopped host: BleHost::stop() closes the link.
        qCWarning(log::lcBle).noquote()
            << "Link close not posted; it is closed when the BLE host stops:"
            << toQString(posted.error().detail);
    }
    state.lock.reset(); // releases the BleLock (owner thread, as in open())
    if (m_registry != nullptr) {
        m_registry->remove(this);
        m_registry = nullptr;
    }
    qCInfo(log::lcBle) << "Session closed";
}

Status BleSession::sendLine(std::string line) {
    if (m_closed) {
        return fail(ErrorCode::Cancelled);
    }
    if (line.size() > ble::NusLink::maxLineBytes) {
        return std::unexpected(
            invalidOption(QCoreApplication::translate(
                              "hio::device", "A command line must not be longer than %1 bytes.")
                              .arg(ble::NusLink::maxLineBytes),
                          std::to_string(line.size())));
    }
    // Review B4: synchronous back-pressure as DeviceSession (R39): the caller keeps the line and
    // sends it again later instead of losing it in the link's queue.
    // Held lines (plan 0021) count too: they are sent when the shell query ends (review P8).
    if (m_linesInFlight.size() + heldLineCount() >= maxLinesInFlight) {
        return std::unexpected(makeError(
            ErrorCode::InternalQueueFull,
            std::format("{} lines are not written yet", m_linesInFlight.size() + heldLineCount())));
    }
    if (Status held; holdLine(line, held)) {
        return held; // plan 0021: a shell query runs (ADR-0175)
    }
    const std::uint64_t origin = sendingOrigin();
    Status posted =
        m_host->post(ble::SendLine{.client = m_internals->client, .line = std::move(line)});
    if (posted) {
        m_linesInFlight.push_back(origin);
        lineQueued();
    }
    return posted;
}

Status BleSession::reconnect() {
    if (m_closed) {
        return fail(ErrorCode::Cancelled);
    }
    // Always tried at once (ADR-0082).
    return m_host->post(ble::ConnectLink{.client = m_internals->client, .automatic = false});
}

Status BleSession::pair() {
    if (m_closed) {
        return fail(ErrorCode::Cancelled);
    }
    return m_host->post(ble::PairDevice{
        .device = m_internals->deviceId, .subscriber = nullptr, .client = m_internals->client});
}

const ConsoleLog& BleSession::log() const { return *m_internals->log; }

SessionIdentity BleSession::identity() const {
    return SessionIdentity{.transport = Transport::Ble,
                           .id = m_internals->deviceId,
                           .displayName = m_internals->deviceName.empty()
                                              ? m_internals->deviceId
                                              : m_internals->deviceName};
}

Capabilities BleSession::capabilities() const { return {}; }

const DeviceProfile& BleSession::profile() const noexcept { return *m_internals->profile; }

std::string_view BleSession::consoleTargetId() const { return m_internals->target->id; }

std::filesystem::path BleSession::consoleFile() const { return m_consoleFile; }

std::optional<int> BleSession::rssi() const { return m_rssi; }

std::uint16_t BleSession::mtu() const { return m_mtu; }

void BleSession::wakeUp() {
    if (!m_wakePending.exchange(true)) {
        QMetaObject::invokeMethod(this, "drainEvents", Qt::QueuedConnection);
    }
}

// Background scan and retries follow the connection state (ADR-0082).
void BleSession::connectionUpdated() {
    detail::BleSessionInternals& state = *m_internals;
    const bool retrying =
        state.autoReconnect && m_connection.state == ConnectionState::Disconnected;
    if (retrying) {
        // Review B2: a scan is (re)started here only when the state changed to Disconnected;
        // later restarts come from the retry timer, paced by the reconnect interval.
        if (!m_scanning && Clock::now() >= m_nextScan) {
            startBackgroundScan();
        }
        m_retryTimer.start();
        return;
    }
    if (m_scanning) {
        stopBackgroundScan();
    }
    m_retryTimer.stop();
    if (m_connection.state == ConnectionState::Connected) {
        m_scanErrorShown.reset();
    }
}

void BleSession::startBackgroundScan() {
    m_scanning = static_cast<bool>(
        m_host->post(ble::StartScan{.subscriber = m_internals->scan, .duration = {}}));
    if (!m_scanning) {
        m_nextScan = Clock::now() + m_internals->timing.reconnectInterval;
    }
}

// A background scan failed (review B2): an adapter that cannot be used ends the session
// (Stopped, plan 0011 Q9); another error is shown once and the scan is retried by the retry timer.
void BleSession::scanFailed(const Error& error) {
    switch (error.code) {
    case ErrorCode::BleAdapterNotFound:
    case ErrorCode::BleAdapterOff:
    case ErrorCode::BlePermissionDenied:
    case ErrorCode::BleStackUnavailable:
        qCWarning(log::lcBle).noquote() << "Background scan failed:" << toQString(error.detail);
        m_retryTimer.stop();
        // Review N3, N4: the BLE thread ends the link with it, so the handler reports Stopped
        // (console file line, no "wait for the console to reconnect") and an automatic attempt
        // still in flight or queued cannot turn it into Disconnected again.
        if (Status posted =
                m_host->post(ble::StopLink{.client = m_internals->client, .error = error});
            !posted) {
            qCWarning(log::lcBle).noquote()
                << "Link stop not posted:" << toQString(posted.error().detail);
            m_connection = ConnectionEvent{.state = ConnectionState::Stopped,
                                           .error = error,
                                           .targetMillivolts = std::nullopt,
                                           .transport = Transport::Ble,
                                           .mtu = std::nullopt};
            emit connectionChanged(m_connection);
        }
        return;
    default:
        break;
    }
    if (m_scanErrorShown != error.code) {
        m_scanErrorShown = error.code;
        emit warning(error);
    }
}

void BleSession::stopBackgroundScan() {
    m_scanning = false;
    if (Status posted = m_host->post(ble::StopScan{.subscriber = m_internals->scan}); !posted) {
        // The host is stopping: its scan ends anyway.
        qCDebug(log::lcBle) << "Background scan stop not posted:"
                            << toQString(posted.error().detail);
    }
}

void BleSession::retryTick() {
    const detail::BleSessionInternals& state = *m_internals;
    if (m_closed || m_attemptPending || m_connection.state != ConnectionState::Disconnected ||
        !state.autoReconnect) {
        return;
    }
    const auto now = Clock::now();
    if (!m_scanning && now >= m_nextScan) {
        startBackgroundScan(); // B2: restarted here only, at most once per reconnect interval
    }
    if (now < m_nextRetry || !m_lastSeen || now - *m_lastSeen > state.timing.seenWindow) {
        return; // only while the device was seen recently
    }
    if (m_host->post(ble::ConnectLink{.client = state.client, .automatic = true})) {
        m_attemptPending = true;
        qCDebug(log::lcBle) << "Reconnecting automatically";
    }
}

void BleSession::drainScan() {
    detail::BleSessionInternals& state = *m_internals;
    // Background scan: only this device's advertisements matter.
    for (std::size_t i = 0; i < ble::BleHost::eventQueueCapacity; ++i) {
        auto event = state.scan->pop();
        if (!event) {
            break;
        }
        if (const auto* advertisement = std::get_if<ble::Advertisement>(&*event);
            advertisement != nullptr && advertisement->id == state.deviceId) {
            m_lastSeen = advertisement->seen;
            m_rssi = advertisement->rssi;
        } else if (const auto* finished = std::get_if<ble::ScanFinished>(&*event)) {
            // Ended by the stack (or failed): restarted by the retry timer (B2).
            m_scanning = false;
            m_nextScan = Clock::now() + state.timing.reconnectInterval;
            if (finished->error && !m_closed &&
                m_connection.state == ConnectionState::Disconnected) {
                scanFailed(*finished->error);
            }
        }
    }
}

// One event of the session's queue; lines are collected in `batch` (emitted before any other
// signal, so the order of the worker is kept). True when the connection state changed.
bool BleSession::handleEvent(detail::BleSessionEvent& event, QList<ConsoleLine>& batch) {
    const auto flushBatch = [&] {
        if (!batch.isEmpty()) {
            emit linesReceived(batch);
            batch.clear();
        }
    };
    bool stateChanged = false;
    std::visit(Overloaded{
                   [&](ConsoleLine& line) {
                       if (line.dir == Direction::In && !m_linesInFlight.empty()) {
                           line.origin = m_linesInFlight.front(); // the echo: written (B4)
                           m_linesInFlight.pop_front();
                       }
                       if (captureLine(line)) {
                           return; // a silent shell query took it (plan 0021, ADR-0175)
                       }
                       line.seq = m_internals->log->append(line);
                       batch.push_back(std::move(line));
                   },
                   [&](ConnectionEvent& connection) {
                       flushBatch();
                       m_connection = connection;
                       if (connection.mtu) {
                           m_mtu = *connection.mtu;
                       }
                       stateChanged = true;
                       emit connectionChanged(m_connection);
                   },
                   [&](const Warning& item) {
                       flushBatch();
                       emit warning(item.error);
                   },
                   [&](const detail::BlePromptSeen&) {
                       flushBatch();
                       emit promptSeen();
                   },
                   [&](const detail::BlePairingPrompt&) {
                       flushBatch();
                       emit pairingPrompt();
                   },
                   [&](const detail::BlePairingFinished& item) {
                       flushBatch();
                       if (!item.status) {
                           emit warning(item.status.error());
                       }
                       emit pairingFinished(item.status);
                   },
                   [&](detail::BleConsoleFileChanged& changed) {
                       m_consoleFile = std::move(changed.file);
                   },
                   [&](const detail::BleLineRejected&) {
                       if (!m_linesInFlight.empty()) {
                           m_linesInFlight.pop_front();
                       }
                   },
                   [&](const detail::BleAttemptEnded&) {
                       m_attemptPending = false;
                       m_nextRetry = Clock::now() + m_internals->timing.reconnectInterval;
                   },
               },
               event);
    return stateChanged;
}

void BleSession::drainEvents() {
    m_wakePending.store(false);
    if (m_closed) {
        return;
    }
    detail::BleSessionInternals& state = *m_internals;
    const std::uint64_t dropped = state.events->droppedCount();
    if (dropped != m_reportedDrops) {
        const std::uint64_t count = dropped - m_reportedDrops;
        m_reportedDrops = dropped;
        qCWarning(log::lcBle) << "Session events dropped (queue full):" << count;
        emit linesDropped(count);
    }
    drainScan();
    QList<ConsoleLine> batch;
    bool stateChanged = false;
    // Bounded per call; anything left is drained by the next (rescheduled) call.
    for (std::size_t i = 0; i < eventQueueCapacity && !m_closed; ++i) {
        auto event = state.events->pop();
        if (!event) {
            break;
        }
        stateChanged = handleEvent(*event, batch) || stateChanged;
    }
    if (m_closed) {
        return;
    }
    if (!batch.isEmpty()) {
        emit linesReceived(batch);
    }
    if (stateChanged) {
        connectionUpdated();
    }
    if (state.events->size() > 0 || state.scan->size() > 0) {
        wakeUp();
    }
}

} // namespace hio::device
