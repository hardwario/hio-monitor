#include "hio/device/device_session.h"

#include "hio/core/bounded_queue.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/device/console_file_sink.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/probe_lock.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/device/session_registry.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/jlink_library.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/rtt_channel.h"
#include "hio/hw/jlink/types.h"
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
    if (target->rttTerminal.empty()) {
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

    auto session =
        std::make_unique<DeviceSession>(DeviceSession::Passkey{}, registry, std::move(internals));
    registry.add(session.get());

    // 4. the worker; no library or probe access happens on this thread.
    DeviceSession* facade = session.get();
    SessionInternals& state = *facade->m_internals;
    state.worker =
        std::jthread([config = std::move(config), factory = std::move(factory),
                      lock = std::move(lock), &state, facade](const std::stop_token& stop) mutable {
            const std::function<void()> wake = [facade] { facade->wakeUp(); };
            runWorker(stop, std::move(config), std::move(factory), std::move(lock), state.commands,
                      state.events, wake);
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

Status DeviceSession::reset(bool halt) {
    return post(*m_internals, m_closed, detail::ResetCommand{.halt = halt});
}

Status DeviceSession::reconnect() {
    return post(*m_internals, m_closed, detail::ReconnectCommand{});
}

Status DeviceSession::suspend() { return post(*m_internals, m_closed, detail::SuspendCommand{}); }

Status DeviceSession::resume() { return post(*m_internals, m_closed, detail::ResumeCommand{}); }

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
                },
                [&](const Warning& item) {
                    flushBatch();
                    emit warning(item.error);
                },
                [&](detail::ProbeIdentified& identified) { m_probe = std::move(identified.probe); },
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
