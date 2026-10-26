#include "hio/device/shell_query.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/device/command_capture.h"
#include "hio/device/console_line.h"
#include "hio/device/console_session.h"
#include "hio/device/device_session.h"
#include "hio/device/session_events.h"

#include <QEventLoop>
#include <QLoggingCategory>
#include <QObject>
#include <QTimer>
#include <QtCore/qtmetamacros.h>
#include <QtLogging>

#include <algorithm>
#include <chrono>
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace hio::device {

Result<std::unique_ptr<ShellQuery>> ShellQuery::start(ConsoleSession& session, std::string command,
                                                      const ShellQueryOptions& options) {
    if (session.connection().state != ConnectionState::Connected) {
        return std::unexpected(makeError(ErrorCode::TargetNotConnected, "shell query " + command));
    }
    const auto* device = qobject_cast<const DeviceSession*>(&session);
    if ((device != nullptr && device->runningOperation()) || (options.busy && options.busy()) ||
        session.shellCaptureActive()) {
        return std::unexpected(makeError(ErrorCode::InternalBusy, "shell query " + command));
    }
    if (session.shellBusyFor() > std::chrono::milliseconds(0)) {
        return std::unexpected(makeError(ErrorCode::InternalBusy,
                                         "shell query " + command + ": the shell is not idle"));
    }
    qCDebug(log::lcDevice).noquote() << "Shell query:" << toQString(command);
    auto capture = CommandCapture::start(session, command,
                                         CaptureOptions{.timeout = options.timeout,
                                                        .silence = CaptureOptions{}.silence,
                                                        .maxLines = options.maxLines,
                                                        .maxBytes = CaptureOptions{}.maxBytes,
                                                        .silent = true});
    if (!capture) {
        return std::unexpected(std::move(capture).error());
    }
    return std::make_unique<ShellQuery>(Passkey{}, std::move(command), std::move(*capture));
}

ShellQuery::ShellQuery(Passkey /*key*/, std::string command,
                       std::unique_ptr<CommandCapture> capture)
    : m_command(std::move(command)), m_capture(std::move(capture)) {
    connect(m_capture.get(), &CommandCapture::finished, this, [this] {
        const CaptureResult& captured = m_capture->result().value_or(CaptureResult{});
        ShellQueryResult result{.lines = {}, .end = captured.end, .truncated = captured.truncated};
        result.lines.reserve(captured.lines.size());
        for (const ConsoleLine& line : captured.lines) {
            qCDebug(log::lcDevice).noquote() << "  " << toQString(line.text);
            result.lines.push_back(line.text);
        }
        qCDebug(log::lcDevice).noquote()
            << "Shell query ended:" << toQString(m_command) << result.lines.size() << "lines,"
            << toQString(captureEndId(result.end));
        m_result = std::move(result);
        m_done = true;
        emit finished();
    });
    connect(m_capture.get(), &CommandCapture::aborted, this, [this] {
        qCDebug(log::lcDevice).noquote()
            << "Shell query ended without a result (disconnected):" << toQString(m_command);
        m_done = true;
        emit finished();
    });
}

ShellQuery::~ShellQuery() = default; // the capture cancels itself

void ShellQuery::cancel() { m_capture->cancel(); }

ShellQueryQueue::ShellQueryQueue(std::function<bool()> busy, QObject* parent)
    : QObject(parent), m_busy(std::move(busy)) {
    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this, &ShellQueryQueue::next);
}

ShellQueryQueue::~ShellQueryQueue() {
    m_query.reset();
    m_pending.clear(); // callbacks are not called during destruction
}

void ShellQueryQueue::setSession(ConsoleSession* session) {
    if (session == m_session) {
        return;
    }
    // Review P4: detached first, so that a callback answered by clear() cannot start a new query
    // on the session being released.
    m_session = session;
    clear();
}

void ShellQueryQueue::run(std::string command, Callback done) {
    if (m_clearing || m_pending.size() >= maxQueued) {
        if (done) {
            done(std::nullopt);
        }
        return;
    }
    m_pending.push_back(Pending{.command = std::move(command), .done = std::move(done)});
    if (!m_query && !m_waiting) {
        next();
    }
}

void ShellQueryQueue::clear() {
    m_clearing = true;
    m_idleTimer.stop();
    m_waiting = false;
    if (m_query) {
        std::unique_ptr<ShellQuery> query = std::move(m_query);
        query->disconnect(this);
        disconnect(m_sessionGone);
        query->cancel();
        query.release()->deleteLater(); // NOLINT(bugprone-unused-return-value)
        finishCurrent(std::nullopt);
    }
    std::deque<Pending> pending = std::move(m_pending);
    m_pending.clear();
    for (Pending& item : pending) {
        if (item.done) {
            item.done(std::nullopt);
        }
    }
    m_clearing = false;
}

ShellQueryQueue::Turn ShellQueryQueue::frontTurn() {
    using std::chrono::milliseconds;
    // Review P1: only on an idle shell, at most maxIdleWait.
    const milliseconds busyFor = m_session ? m_session->shellBusyFor() : milliseconds(0);
    if (busyFor == milliseconds(0)) {
        m_waiting = false;
        return Turn::Ready;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!m_waiting) {
        m_waiting = true;
        m_waitingSince = now;
    }
    if (now - m_waitingSince < maxIdleWait) {
        m_idleTimer.start(std::clamp(busyFor, milliseconds(10), milliseconds(300)));
        return Turn::Wait;
    }
    qCDebug(log::lcDevice).noquote()
        << "Shell query not run (the shell stayed busy):" << toQString(m_pending.front().command);
    m_waiting = false;
    return Turn::Expired;
}

void ShellQueryQueue::next() {
    while (!m_query && !m_pending.empty()) {
        const Turn turn = frontTurn();
        if (turn == Turn::Wait) {
            return;
        }
        Pending item = std::move(m_pending.front());
        m_pending.pop_front();
        if (turn == Turn::Expired || !m_session || !startQuery(item)) {
            if (item.done) {
                item.done(std::nullopt);
            }
        }
    }
}

bool ShellQueryQueue::startQuery(Pending& item) {
    auto query = ShellQuery::start(*m_session, item.command,
                                   ShellQueryOptions{.timeout = ShellQuery::defaultTimeout,
                                                     .maxLines = ShellQuery::defaultMaxLines,
                                                     .busy = m_busy});
    if (!query) {
        qCDebug(log::lcDevice).noquote() << "Shell query not run:" << toQString(item.command)
                                         << toQString(idOf(query.error().code));
        return false;
    }
    m_query = std::move(*query);
    m_currentDone = std::move(item.done);
    const ShellQuery* started = m_query.get();
    connect(m_query.get(), &ShellQuery::finished, this,
            [this, started] { endQuery(started, started->result()); });
    // A session that goes away ends the query (its capture stops without finished()).
    m_sessionGone = connect(m_session, &QObject::destroyed, this,
                            [this, started] { endQuery(started, std::nullopt); });
    return true;
}

void ShellQueryQueue::endQuery(const ShellQuery* query,
                               const std::optional<ShellQueryResult>& result) {
    // Review P5: only the running query, once.
    if (query == nullptr || m_query.get() != query) {
        return;
    }
    m_query->disconnect(this);
    disconnect(m_sessionGone);
    // Deleted later: this may run inside the query's own signal.
    m_query.release()->deleteLater(); // NOLINT(bugprone-unused-return-value)
    finishCurrent(result);            // the released query lives until the event loop deletes it
    next();
}

void ShellQueryQueue::finishCurrent(const std::optional<ShellQueryResult>& result) {
    Callback done = std::move(m_currentDone);
    m_currentDone = {};
    if (done) {
        done(result);
    }
}

std::optional<ShellQueryResult> waitForQuery(ShellQuery& query) {
    if (!query.isFinished()) {
        QEventLoop loop;
        QObject::connect(&query, &ShellQuery::finished, &loop, &QEventLoop::quit);
        // A session that goes away stops the capture without finished(): the timeout is the bound.
        QTimer guard;
        guard.setSingleShot(true);
        QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
        guard.start(ShellQuery::defaultTimeout * 5);
        loop.exec();
    }
    return query.result();
}

std::optional<ShellQueryResult> runAndWait(ShellQueryQueue& queue, std::string command) {
    std::optional<ShellQueryResult> answer;
    bool answered = false;
    QEventLoop loop;
    queue.run(std::move(command), [&](const std::optional<ShellQueryResult>& result) {
        answer = result;
        answered = true;
        loop.quit();
    });
    if (!answered) {
        // Bounded by the queue: the idle wait, then the query's own timeout.
        QTimer guard;
        guard.setSingleShot(true);
        QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
        guard.start(ShellQueryQueue::maxIdleWait + ShellQuery::defaultTimeout * 2);
        loop.exec();
    }
    if (!answered) {
        queue.clear(); // the callback captures locals of this frame
    }
    return answer;
}

} // namespace hio::device
