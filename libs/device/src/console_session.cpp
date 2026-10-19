#include "hio/device/console_session.h"

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/session_events.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaObject>
#include <QObject>
#include <QTimer>

#include <chrono>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <stop_token>
#include <utility>

namespace hio::device {

void ConsoleSession::close() {
    if (m_closeCalled) {
        return;
    }
    m_closeCalled = true;
    closeSession();
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
