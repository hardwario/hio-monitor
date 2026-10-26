#include "hio/device/console_session.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/device/command_capture.h"
#include "hio/device/console_line.h"
#include "hio/device/session_events.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QObject>
#include <QTimer>
#include <QtCore/qtmetamacros.h>
#include <QtLogging>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>

namespace hio::device {

using ShellClock = std::chrono::steady_clock;

void ConsoleSession::close() {
    if (m_closeCalled) {
        return;
    }
    m_closeCalled = true;
    closeSession();
}

ConsoleSession::ConsoleSession() {
    // Shell activity for shellBusyFor() (connected before any user of the signals).
    connect(this, &ConsoleSession::promptSeen, this, [this] {
        m_promptPending = false;
        m_lastShellActivity = ShellClock::now();
    });
    connect(this, &ConsoleSession::connectionChanged, this, [this](const ConnectionEvent& event) {
        if (event.state != ConnectionState::Connected) {
            m_unechoed = 0;
            m_promptPending = false;
        }
    });
}

void ConsoleSession::setShellCapture(ShellCapture capture) {
    m_capture = std::move(capture);
    if (m_capture) {
        return;
    }
    // The held user lines, in order (a full transport queue drops the rest, logged).
    while (!m_held.empty() && !m_capture) {
        std::string line = std::move(m_held.front());
        m_held.pop_front();
        if (const Status sent = sendLine(std::move(line)); !sent) {
            qCWarning(log::lcDevice).noquote()
                << "A held command was not sent:" << toQString(sent.error().detail);
        }
    }
    if (!m_capture) {
        emit shellCaptureEnded();
    }
}

Status ConsoleSession::sendCaptureLine(std::string line, std::uint64_t origin) {
    m_sendingOrigin = origin;
    Status sent = sendLine(std::move(line));
    m_sendingOrigin = 0;
    return sent;
}

std::chrono::milliseconds ConsoleSession::shellBusyFor() const {
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
    constexpr milliseconds poll{50};
    const ShellClock::time_point now = ShellClock::now();
    if (m_unechoed > 0 && now - m_lastSent < unechoedExpiry) {
        return poll; // a sent line has no echo yet
    }
    const ShellClock::time_point last = std::max(m_lastShellActivity, m_lastSent);
    const auto quiet = duration_cast<milliseconds>(now - last);
    if (!shellPromptOf(*this).empty()) {
        // A command that never prints the prompt again does not block the queries for good: the
        // wait expires after promptWaitExpiry without shell output.
        if (!m_promptPending || quiet >= promptWaitExpiry) {
            return milliseconds(0);
        }
        return std::min(poll, promptWaitExpiry - quiet);
    }
    return quiet >= shellQuietTime ? milliseconds(0) : shellQuietTime - quiet;
}

bool ConsoleSession::captureLine(const ConsoleLine& line) {
    if (line.channel != Channel::Shell) {
        return false;
    }
    m_lastShellActivity = ShellClock::now();
    if (line.dir == Direction::In) {
        if (m_unechoed > 0) {
            --m_unechoed;
        }
        m_promptPending = true; // until the prompt after its output
    }
    return m_capture && m_capture(line);
}

bool ConsoleSession::holdLine(std::string& line, Status& status) {
    if (!m_capture || m_sendingOrigin != 0) {
        return false;
    }
    if (m_held.size() >= maxHeldLines) {
        status = std::unexpected(
            makeError(ErrorCode::InternalQueueFull,
                      std::format("{} commands wait for a running shell query", m_held.size())));
        return true;
    }
    m_held.push_back(std::move(line));
    status = {};
    return true;
}

void ConsoleSession::lineQueued() {
    ++m_unechoed;
    m_lastSent = ShellClock::now();
}

namespace {

using FinalResult = std::function<std::optional<Status>(const ConnectionEvent&)>;

// Runs a local event loop until `finalResult` gives a result for a connection event (the current
// one first), `timeout` (timeout) or a stop request (cancelled).
Status waitForConnection(ConsoleSession& session, std::chrono::milliseconds timeout,
                         const std::stop_token& stop, const FinalResult& finalResult) {
    if (auto result = finalResult(session.connection())) {
        return *result;
    }
    std::optional<Status> result;
    QEventLoop loop;
    const QMetaObject::Connection connection = QObject::connect(
        &session, &ConsoleSession::connectionChanged, &loop, [&](const ConnectionEvent& event) {
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

Status waitUntilConnected(ConsoleSession& session, std::chrono::milliseconds timeout,
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

Status waitForFirstAttempt(ConsoleSession& session, std::chrono::milliseconds timeout,
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
