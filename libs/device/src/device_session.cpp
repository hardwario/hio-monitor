#include "hio/device/device_session.h"

#include "hio/core/bounded_queue.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/core/settings.h"
#include "hio/device/console_file_sink.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/operation.h"
#include "hio/device/probe_lock.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/device/session_registry.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/lte_input.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/jlink_library.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/rtt_channel.h"
#include "hio/hw/jlink/types.h"
#include "hio/hw/nrfutil/nrfutil.h"
#include "session_internal.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QList>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtCore/qnamespace.h>
#include <QtCore/qtmetamacros.h>
#include <QtLogging>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

namespace hio::device {

namespace {

template <class... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
};

Error invalidOption(const QString& message, std::string detail) {
    Error error = makeError(ErrorCode::UsageInvalidArgument, std::move(detail));
    error.message = message.toStdString();
    return error;
}

// Step 1 of open() (ADR-0053): the options.
Result<const TargetProfile*> validate(const SessionOptions& options) {
    if (options.profile == nullptr) {
        return std::unexpected(invalidOption(
            QCoreApplication::translate("hio::device", "No device profile was given."), {}));
    }
    const TargetProfile* target = findTarget(*options.profile, options.targetId);
    if (target == nullptr) {
        return std::unexpected(invalidOption(
            QCoreApplication::translate("hio::device", "The device %1 has no target \"%2\".")
                .arg(toQString(options.profile->displayName), toQString(options.targetId)),
            {}));
    }
    if (target->rttTerminal.empty() && options.attachRtt) {
        return std::unexpected(invalidOption(
            QCoreApplication::translate("hio::device", "The %1 target of %2 has no console.")
                .arg(toQString(target->id), toQString(options.profile->displayName)),
            {}));
    }
    if (options.speedKhz && (*options.speedKhz < minSpeedKhz || *options.speedKhz > maxSpeedKhz)) {
        return std::unexpected(
            invalidOption(QCoreApplication::translate(
                              "hio::device", "The J-Link speed must be between %1 and %2 kHz.")
                              .arg(minSpeedKhz)
                              .arg(maxSpeedKhz),
                          std::to_string(*options.speedKhz)));
    }
    if (options.latencyMs < minLatencyMs || options.latencyMs > maxLatencyMs) {
        return std::unexpected(invalidOption(
            QCoreApplication::translate("hio::device", "The latency must be between %1 and %2 ms.")
                .arg(minLatencyMs)
                .arg(maxLatencyMs),
            std::to_string(options.latencyMs)));
    }
    return target;
}

} // namespace

std::optional<std::string> slowSpeedWarning(std::optional<int> speedKhz) {
    if (!jlink::isSlowRttSpeed(speedKhz)) {
        return std::nullopt;
    }
    return QCoreApplication::translate("hio::device",
                                       "At this J-Link speed the console may not be found within "
                                       "%1 s. Use auto or %2 kHz or more.")
        .arg(std::chrono::duration_cast<std::chrono::seconds>(jlink::rttAttachDeadline(speedKhz))
                 .count())
        .arg(jlink::rttFullSpeedKhz)
        .toStdString();
}

std::string_view connectionStateId(ConnectionState state) noexcept {
    switch (state) {
    case ConnectionState::Connecting:
        return "connecting";
    case ConnectionState::Connected:
        return "connected";
    case ConnectionState::Disconnected:
        return "disconnected";
    case ConnectionState::Stopped:
        return "stopped";
    }
    return "stopped";
}

namespace detail {

std::optional<std::uint32_t> SessionInternals::requestedSerial(const DeviceSession& session) {
    return session.m_internals->requestedSn;
}

std::optional<std::pair<OperationId, OperationResult>>
SessionInternals::abandonRunning(Error error) {
    if (!runningOperation) {
        return std::nullopt;
    }
    const OperationId id = *runningOperation;
    runningOperation.reset();
    qCWarning(log::lcDevice).noquote()
        << "Operation" << id << "ended without a worker result:" << toQString(error.message)
        << toQString(error.detail);
    lastFinished = std::pair(id, OperationResult{.kind = runningKind,
                                                 .status = std::unexpected(std::move(error)),
                                                 .duration = {},
                                                 .chip = std::nullopt,
                                                 .data = {}});
    return lastFinished;
}

nrfutil::LocatorOptions nrfUtilLocator(const SessionOptions& options) {
    if (options.nrfUtil) {
        return *options.nrfUtil;
    }
    auto settings = Settings::openDefault();
    return nrfutil::defaultLocatorOptions(std::nullopt, settings ? &*settings : nullptr);
}

bool SessionInternals::takeQueuedResult() {
    while (auto event = events.pop({}, std::chrono::milliseconds(0))) {
        if (auto* finishedEvent = std::get_if<OperationFinishedEvent>(&*event);
            finishedEvent != nullptr && runningOperation == finishedEvent->id) {
            runningOperation.reset();
            lastFinished = std::pair(finishedEvent->id, std::move(finishedEvent->result));
            return true;
        }
    }
    return false;
}

std::optional<OperationResult> SessionInternals::finished(const DeviceSession& session,
                                                          OperationId id) {
    const auto& last = session.m_internals->lastFinished;
    if (last && last->first == id) {
        return last->second;
    }
    return std::nullopt;
}

Result<std::unique_ptr<DeviceSession>> openSession(SessionRegistry& registry,
                                                   jlink::LibraryLocation location,
                                                   SessionOptions options, OpenHooks hooks) {
    return SessionInternals::open(registry, std::move(location), std::move(options),
                                  std::move(hooks));
}

Result<std::unique_ptr<DeviceSession>> SessionInternals::open(SessionRegistry& registry,
                                                              jlink::LibraryLocation location,
                                                              SessionOptions options,
                                                              OpenHooks hooks) {
    // 1. options
    auto target = validate(options);
    if (!target) {
        return std::unexpected(std::move(target).error());
    }
    // 2. the in-process registry slot
    if (auto available = registry.checkAvailable(options.probeSn); !available) {
        return std::unexpected(std::move(available).error());
    }
    // 3. the cross-process probe lock (ADR-0058), when the probe is known already
    std::optional<ProbeLock> lock;
    if (options.probeSn) {
        auto acquired = ProbeLock::acquire(*options.probeSn, registry.options().lockDirectory);
        if (!acquired) {
            return std::unexpected(std::move(acquired).error());
        }
        lock = std::move(*acquired);
    }

    auto internals = std::make_unique<SessionInternals>();
    internals->profile = options.profile;
    internals->target = *target;
    internals->requestedSn = options.probeSn;
    internals->log = options.consoleLog ? options.consoleLog : std::make_shared<ConsoleLog>();

    WorkerConfig config;
    config.profile = options.profile;
    config.target = *target;
    config.probeSn = options.probeSn;
    config.speedKhz = options.speedKhz;
    config.latencyMs = options.latencyMs;
    config.autoReconnect = options.autoReconnect;
    config.resetOnOpen = options.resetOnOpen;
    config.attachRtt = options.attachRtt;
    config.lockDirectory = registry.options().lockDirectory;
    config.timing = hooks.timing;
    // ADR-0110: the attach deadline scales with a fixed low speed (the base is the hooks' value,
    // 15 s in production).
    config.timing.rtt.attachDeadline =
        jlink::rttAttachDeadline(options.speedKhz, hooks.timing.rtt.attachDeadline);
    config.cycles = std::move(hooks.cycles);
    if (!options.consoleFile || !options.consoleFile->empty()) {
        config.consoleFile = ConsoleFileConfig{.explicitFile = options.consoleFile,
                                               .directory = registry.options().consoleDirectory,
                                               .profileId = std::string(options.profile->id),
                                               .targetId = std::string((*target)->id),
                                               .lockDirectory = registry.options().lockDirectory};
    }

    ApiFactory factory = std::move(hooks.apiFactory);
    if (!factory) {
        factory = [location = std::move(location), root = registry.options().instanceRoot]()
            -> Result<std::unique_ptr<jlink::IJLinkApi>> {
            auto library = jlink::JLinkLibrary::loadCopy(location, root);
            if (!library) {
                return std::unexpected(std::move(library).error());
            }
            return std::unique_ptr<jlink::IJLinkApi>(std::move(*library));
        };
    }

    ModemUpdaterFactory modemFactory = std::move(hooks.modemUpdaterFactory);
    if (!modemFactory) {
        modemFactory = [locator = nrfUtilLocator(
                            options)]() -> Result<std::unique_ptr<nrfutil::IModemUpdater>> {
            auto executable = nrfutil::locate(locator);
            if (!executable) {
                return std::unexpected(std::move(executable).error());
            }
            auto info = nrfutil::checkVersions(*executable);
            if (!info) {
                return std::unexpected(std::move(info).error());
            }
            return std::make_unique<nrfutil::NrfUtilModemUpdater>(info->executable);
        };
    }

    auto session =
        std::make_unique<DeviceSession>(DeviceSession::Passkey{}, registry, std::move(internals));
    registry.add(session.get());

    // 4. the worker; no library or probe access happens on this thread.
    DeviceSession* facade = session.get();
    SessionInternals& state = *facade->m_internals;
    state.worker = std::jthread([config = std::move(config), factory = std::move(factory),
                                 modemFactory = std::move(modemFactory), lock = std::move(lock),
                                 &state, facade](const std::stop_token& stop) mutable {
        const std::function<void()> wake = [facade] { facade->wakeUp(); };
        runWorker(stop, std::move(config), std::move(factory), std::move(modemFactory),
                  std::move(lock), state.commands, state.events, wake, state.workerFailed);
    });
    qCInfo(log::lcDevice).noquote()
        << QStringLiteral("Session opened: %1 %2, J-Link %3")
               .arg(toQString(options.profile->id), toQString((*target)->id),
                    options.probeSn ? QString::number(*options.probeSn)
                                    : QStringLiteral("first free"));
    return session;
}

} // namespace detail

Result<std::unique_ptr<DeviceSession>> DeviceSession::open(SessionRegistry& registry,
                                                           jlink::LibraryLocation location,
                                                           SessionOptions options) {
    return detail::openSession(registry, std::move(location), std::move(options),
                               detail::OpenHooks{});
}

DeviceSession::DeviceSession(Passkey /*key*/, SessionRegistry& registry,
                             std::unique_ptr<detail::SessionInternals> internals)
    : m_registry(&registry), m_internals(std::move(internals)) {}

DeviceSession::~DeviceSession() { close(); }

void DeviceSession::close() {
    if (m_closed) {
        return;
    }
    m_closed = true;
    m_internals->commands.close();
    if (m_internals->worker.joinable()) {
        m_internals->worker.request_stop();
        m_internals->worker.join(); // the worker releases the probe lock and the library copy
    }
    // Review R13: the worker may have finished the operation after a waiter gave up: its real
    // result (success or a real error) wins over a synthesized one. Review R4: otherwise the
    // operation ends as cancelled (or internal.unexpected when the worker died). No signal after
    // close(); waitForOperation() / finishedResult() pick the result up.
    if (m_internals->runningOperation && !m_internals->takeQueuedResult()) {
        (void)m_internals->abandonRunning(
            m_internals->workerFailed.load()
                ? makeError(ErrorCode::InternalUnexpected, "the session worker died")
                : makeError(ErrorCode::Cancelled, "the session was closed"));
    }
    m_internals->events.close();
    if (m_registry != nullptr) {
        m_registry->remove(this);
        m_registry = nullptr;
    }
    qCInfo(log::lcDevice) << "Session closed";
}

namespace {

Status post(detail::SessionInternals& internals, bool closed, detail::Command command) {
    if (closed) {
        return fail(ErrorCode::Cancelled);
    }
    return internals.commands.push(std::move(command));
}

} // namespace

Status DeviceSession::sendLine(std::string line) {
    return post(*m_internals, m_closed, detail::SendLineCommand{.line = std::move(line)});
}

Status DeviceSession::reconnect() {
    return post(*m_internals, m_closed, detail::ReconnectCommand{});
}

Status DeviceSession::suspend() { return post(*m_internals, m_closed, detail::SuspendCommand{}); }

Status DeviceSession::resume() { return post(*m_internals, m_closed, detail::ResumeCommand{}); }

// ---- operations ----------------------------------------------------------------------------

Result<OperationId> DeviceSession::startOperation(detail::OperationCommand command,
                                                  std::string_view targetId) {
    if (m_closed) {
        return fail(ErrorCode::Cancelled);
    }
    const TargetProfile* target = findTarget(*m_internals->profile, targetId);
    if (target == nullptr) {
        return std::unexpected(invalidOption(
            QCoreApplication::translate("hio::device", "The device %1 has no target \"%2\".")
                .arg(toQString(m_internals->profile->displayName), toQString(targetId)),
            {}));
    }
    if (m_internals->workerFailed.load()) {
        // Review R14: nothing would ever run or finish it.
        return std::unexpected(
            makeError(ErrorCode::InternalUnexpected, "the session worker is not running"));
    }
    if (m_internals->runningOperation) {
        return std::unexpected(
            makeError(ErrorCode::InternalBusy,
                      std::format("operation {} is running", *m_internals->runningOperation)));
    }
    const OperationId id = m_internals->nextOperation++;
    m_internals->operationStop = std::stop_source();
    command.id = id;
    command.target = target;
    command.stop = m_internals->operationStop.get_token();
    const OperationKind kind = command.kind;
    if (Status queued = m_internals->commands.push(std::move(command)); !queued) {
        return std::unexpected(std::move(queued).error());
    }
    m_internals->runningOperation = id;
    m_internals->runningKind = kind;
    qCInfo(log::lcDevice).noquote()
        << QStringLiteral("Operation %1 (%2) on %3 queued")
               .arg(id)
               .arg(toQString(operationKindId(kind)), toQString(target->id));
    return id;
}

Result<OperationId> DeviceSession::flash(std::string_view targetId,
                                         std::shared_ptr<const firmware::FirmwareImage> image,
                                         FlashOptions options) {
    if (!image || image->empty()) {
        return std::unexpected(invalidOption(
            QCoreApplication::translate("hio::device", "The firmware image is empty."), {}));
    }
    detail::OperationCommand command;
    command.kind = OperationKind::Flash;
    command.image = std::move(image);
    command.flash = options;
    return startOperation(std::move(command), targetId);
}

Result<OperationId> DeviceSession::flashLteBundle(std::string_view targetId,
                                                  std::shared_ptr<const firmware::LteBundle> bundle,
                                                  FlashOptions options) {
    if (!bundle || bundle->application.empty() || bundle->modemZip.empty()) {
        return std::unexpected(invalidOption(
            QCoreApplication::translate("hio::device", "The LTE bundle is incomplete."), {}));
    }
    detail::OperationCommand command;
    command.kind = OperationKind::LteBundle;
    command.bundle = std::move(bundle);
    command.flash = options;
    return startOperation(std::move(command), targetId);
}

Result<OperationId> DeviceSession::erase(std::string_view targetId, EraseScope scope) {
    detail::OperationCommand command;
    command.kind = scope == EraseScope::Code ? OperationKind::EraseCode : OperationKind::EraseAll;
    return startOperation(std::move(command), targetId);
}

Result<OperationId> DeviceSession::recover(std::string_view targetId) {
    detail::OperationCommand command;
    command.kind = OperationKind::Recover;
    return startOperation(std::move(command), targetId);
}

Result<OperationId> DeviceSession::updateModem(std::string_view targetId,
                                               std::filesystem::path modemZip) {
    if (modemZip.empty()) {
        return std::unexpected(invalidOption(
            QCoreApplication::translate("hio::device", "No modem firmware file was given."), {}));
    }
    detail::OperationCommand command;
    command.kind = OperationKind::Modem;
    command.modemZip = std::move(modemZip);
    return startOperation(std::move(command), targetId);
}

Result<OperationId> DeviceSession::reset(std::string_view targetId, ResetMode mode) {
    detail::OperationCommand command;
    command.kind = OperationKind::Reset;
    command.reset = mode;
    return startOperation(std::move(command), targetId);
}

Result<OperationId> DeviceSession::readMemory(std::string_view targetId, std::uint32_t address,
                                              std::size_t size) {
    if (size == 0 || size > maxReadMemoryBytes) {
        return std::unexpected(
            invalidOption(QCoreApplication::translate(
                              "hio::device", "A memory read must be between 1 and %1 bytes.")
                              .arg(maxReadMemoryBytes),
                          std::to_string(size)));
    }
    detail::OperationCommand command;
    command.kind = OperationKind::ReadMemory;
    command.address = address;
    command.size = size;
    return startOperation(std::move(command), targetId);
}

Status DeviceSession::cancel(OperationId id) {
    if (!m_internals->runningOperation || *m_internals->runningOperation != id) {
        return std::unexpected(invalidOption(
            QCoreApplication::translate("hio::device", "Operation %1 is not running.").arg(id),
            {}));
    }
    qCInfo(log::lcDevice) << "Operation" << id << "cancel requested";
    m_internals->operationStop.request_stop(); // thread-safe; the worker checks it
    return {};
}

std::optional<OperationResult> DeviceSession::finishedResult(OperationId id) const {
    return detail::SessionInternals::finished(*this, id);
}

std::optional<OperationId> DeviceSession::runningOperation() const {
    return m_internals->runningOperation;
}

const ConsoleLog& DeviceSession::log() const { return *m_internals->log; }

jlink::ProbeInfo DeviceSession::probe() const { return m_probe; }

const DeviceProfile& DeviceSession::profile() const noexcept { return *m_internals->profile; }

const TargetProfile& DeviceSession::target() const noexcept { return *m_internals->target; }

void DeviceSession::wakeUp() {
    // One coalesced queued call (ADR-0031); called by the worker thread.
    if (!m_wakePending.exchange(true)) {
        QMetaObject::invokeMethod(this, "drainEvents", Qt::QueuedConnection);
    }
}

void DeviceSession::drainEvents() {
    m_wakePending.store(false);
    if (m_closed) {
        return;
    }
    BoundedQueue<detail::WorkerEvent>& events = m_internals->events;
    const std::uint64_t dropped = events.droppedCount();
    if (dropped != m_reportedDrops) {
        const std::uint64_t count = dropped - m_reportedDrops;
        m_reportedDrops = dropped;
        qCWarning(log::lcDevice) << "Session events dropped (queue full):" << count;
        emit linesDropped(count);
    }
    QList<ConsoleLine> batch;
    const auto flushBatch = [&] {
        if (!batch.isEmpty()) {
            emit linesReceived(batch);
            batch.clear();
        }
    };
    // Bounded per call; anything left is drained by the next (rescheduled) call.
    for (std::size_t i = 0; i < eventQueueCapacity && !m_closed; ++i) {
        auto event = events.pop({}, std::chrono::milliseconds(0));
        if (!event) {
            break;
        }
        std::visit(
            Overloaded{
                [&](ConsoleLine& line) {
                    line.seq = m_internals->log->append(line);
                    batch.push_back(std::move(line));
                },
                [&](ConnectionEvent& connection) {
                    flushBatch();
                    m_connection = connection;
                    emit connectionChanged(m_connection);
                    // Review R4: the worker died (exception) - its operation never finishes.
                    if (connection.state == ConnectionState::Stopped && connection.error &&
                        connection.error->code == ErrorCode::InternalUnexpected) {
                        if (auto ended = m_internals->abandonRunning(*connection.error)) {
                            emit operationFinished(ended->first, ended->second);
                        }
                    }
                },
                [&](const Warning& item) {
                    flushBatch();
                    emit warning(item.error);
                },
                [&](detail::ProbeIdentified& identified) { m_probe = std::move(identified.probe); },
                [&](const detail::OperationProgressEvent& item) {
                    flushBatch();
                    emit operationProgress(item.id, item.progress);
                },
                [&](const detail::OperationFinishedEvent& item) {
                    flushBatch();
                    if (m_internals->runningOperation == item.id) {
                        m_internals->runningOperation.reset();
                    }
                    m_internals->lastFinished = std::pair(item.id, item.result);
                    emit operationFinished(item.id, item.result);
                },
            },
            *event);
    }
    if (!m_closed) {
        flushBatch();
        if (events.size() > 0) {
            wakeUp();
        }
    }
}

namespace {

using FinalResult = std::function<std::optional<Status>(const ConnectionEvent&)>;

// Runs a local event loop until `finalResult` gives a result for a connection event (the current
// one first), `timeout` (timeout) or a stop request (cancelled).
Status waitForConnection(DeviceSession& session, std::chrono::milliseconds timeout,
                         const std::stop_token& stop, const FinalResult& finalResult) {
    if (auto result = finalResult(session.connection())) {
        return *result;
    }
    std::optional<Status> result;
    QEventLoop loop;
    const QMetaObject::Connection connection = QObject::connect(
        &session, &DeviceSession::connectionChanged, &loop, [&](const ConnectionEvent& event) {
            if (!result) {
                result = finalResult(event);
                if (result) {
                    loop.quit();
                }
            }
        });
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
        if (!result) {
            Error error = makeError(ErrorCode::Timeout,
                                    std::format("not connected within {} ms", timeout.count()));
            error.message =
                QCoreApplication::translate("hio::device", "The device did not connect in time.")
                    .toStdString();
            result = std::unexpected(std::move(error));
        }
        loop.quit();
    });
    QTimer stopPoll;
    QObject::connect(&stopPoll, &QTimer::timeout, &loop, [&] {
        if (stop.stop_requested() && !result) {
            result = fail(ErrorCode::Cancelled);
            loop.quit();
        }
    });
    deadline.start(timeout);
    stopPoll.start(std::chrono::milliseconds(50));
    loop.exec();
    QObject::disconnect(connection);
    return result.value_or(Status{});
}

// Stopped: with its error, or cancelled when stopped on purpose (suspend(); review fix 9 - the
// session will not connect by itself, so waiting longer is pointless).
std::optional<Status> stoppedResult(const ConnectionEvent& event) {
    if (event.state != ConnectionState::Stopped) {
        return std::nullopt;
    }
    if (event.error) {
        return std::unexpected(*event.error);
    }
    return fail(ErrorCode::Cancelled);
}

} // namespace

Status waitUntilConnected(DeviceSession& session, std::chrono::milliseconds timeout,
                          const std::stop_token& stop) {
    return waitForConnection(session, timeout, stop,
                             [](const ConnectionEvent& event) -> std::optional<Status> {
                                 if (event.state == ConnectionState::Connected) {
                                     return Status{};
                                 }
                                 if (event.state == ConnectionState::Disconnected && event.error) {
                                     return std::unexpected(*event.error);
                                 }
                                 return stoppedResult(event);
                             });
}

OperationResult waitForOperation(DeviceSession& session, OperationId id,
                                 const std::function<void(const OperationProgress&)>& onProgress,
                                 const std::stop_token& stop,
                                 std::chrono::milliseconds cancelGrace) {
    std::optional<OperationResult> result = detail::SessionInternals::finished(session, id);
    QEventLoop loop;
    const QMetaObject::Connection progress =
        QObject::connect(&session, &DeviceSession::operationProgress, &loop,
                         [&](OperationId operation, const OperationProgress& event) {
                             if (operation == id && onProgress) {
                                 onProgress(event);
                             }
                         });
    const QMetaObject::Connection finished =
        QObject::connect(&session, &DeviceSession::operationFinished, &loop,
                         [&](OperationId operation, const OperationResult& event) {
                             if (operation == id && !result) {
                                 result = event;
                                 loop.quit();
                             }
                         });
    std::optional<std::chrono::steady_clock::time_point> cancelRequested;
    bool gaveUp = false;
    QTimer stopPoll;
    QObject::connect(&stopPoll, &QTimer::timeout, &loop, [&] {
        if (cancelRequested && std::chrono::steady_clock::now() - *cancelRequested > cancelGrace) {
            gaveUp = true; // review R4: the operation did not stop within the grace period
            loop.quit();
            return;
        }
        if (stop.stop_requested() && !cancelRequested) {
            cancelRequested = std::chrono::steady_clock::now();
            if (const Status cancelled = session.cancel(id); !cancelled) {
                qCDebug(log::lcDevice) << "Operation" << id << "not cancelled (finished)";
            } // otherwise the worker ends it with `cancelled`
        }
        if (!session.runningOperation() && !result) {
            loop.quit(); // e.g. the session was closed
        }
    });
    stopPoll.start(std::chrono::milliseconds(50));
    if (!result) {
        loop.exec();
    }
    QObject::disconnect(progress);
    QObject::disconnect(finished);
    if (!result) {
        result = detail::SessionInternals::finished(session, id); // e.g. ended by close()
    }
    if (!result) {
        return OperationResult{
            .kind = OperationKind::Reset,
            .status = std::unexpected(
                makeError(ErrorCode::Cancelled,
                          gaveUp ? std::format("operation {} did not stop within {} ms "
                                               "after the cancel request",
                                               id, cancelGrace.count())
                                 : std::format("operation {} ended without a result", id))),
            .duration = {},
            .chip = std::nullopt,
            .data = {}};
    }
    return std::move(*result);
}

Status waitForFirstAttempt(DeviceSession& session, std::chrono::milliseconds timeout,
                           const std::stop_token& stop) {
    return waitForConnection(session, timeout, stop,
                             [](const ConnectionEvent& event) -> std::optional<Status> {
                                 if (event.state == ConnectionState::Connected ||
                                     event.state == ConnectionState::Disconnected) {
                                     return Status{};
                                 }
                                 return stoppedResult(event);
                             });
}

} // namespace hio::device
