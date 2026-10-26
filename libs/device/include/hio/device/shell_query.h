#pragma once

// ShellQuery - a silent shell query on a console session (plan 0021 phase 2, ADR-0175, ADR-0170,
// ADR-0166): `info show`, `help`, `<command> -h`.
//
// Built on CommandCapture in silent mode: the command's echo and output never reach the console
// log (they are written to the app log at debug level); a user sendLine() meanwhile waits until the
// query ends (ConsoleSession hold). Ends by the ADR-0104 rule, at most `timeout` (2 s) and
// `maxLines` (2 000) lines. No query while the console is not connected, while an operation runs
// (DeviceSession::runningOperation) or `busy` says so, while another query runs on the session,
// or while the shell is not idle (ConsoleSession::shellBusyFor(), review P1). A console that
// leaves Connected ends the query without a result (review P3).
//
// Thread safety: owner thread of the session.

#include "hio/core/result.h"
#include "hio/device/command_capture.h"
#include "hio/device/console_session.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hio::device {

struct ShellQueryOptions {
    std::chrono::milliseconds timeout{2000};
    std::size_t maxLines = 2000;
    // Extra busy condition of the caller (e.g. the GUI tab's busy page); empty = none.
    std::function<bool()> busy;
};

struct ShellQueryResult {
    std::vector<std::string> lines; // the output lines (text)
    CaptureEnd end = CaptureEnd::Timeout;
    bool truncated = false;
};

class ShellQuery : public QObject {
    Q_OBJECT

    struct Passkey {
        explicit Passkey() = default;
    };

public:
    static constexpr std::chrono::milliseconds defaultTimeout{2000};
    static constexpr std::size_t defaultMaxLines = 2000;

    // Errors: target.not_connected (the console is not connected), internal.busy (an operation,
    // `busy`, another query or a shell that is not idle), and those of ConsoleSession::sendLine.
    [[nodiscard]] static Result<std::unique_ptr<ShellQuery>>
    start(ConsoleSession& session, std::string command, const ShellQueryOptions& options = {});

    ShellQuery(Passkey key, std::string command, std::unique_ptr<CommandCapture> capture);
    ~ShellQuery() override;
    ShellQuery(const ShellQuery&) = delete;
    ShellQuery& operator=(const ShellQuery&) = delete;
    ShellQuery(ShellQuery&&) = delete;
    ShellQuery& operator=(ShellQuery&&) = delete;

    [[nodiscard]] const std::string& command() const noexcept { return m_command; }
    // finished() was emitted; result() is nullopt when the console left Connected meanwhile.
    [[nodiscard]] bool isFinished() const noexcept { return m_done; }
    [[nodiscard]] const std::optional<ShellQueryResult>& result() const noexcept {
        return m_result;
    }
    // Ends the query without a result (no finished()); held user lines are sent.
    void cancel();

signals:
    void finished();

private:
    std::string m_command;
    std::unique_ptr<CommandCapture> m_capture;
    std::optional<ShellQueryResult> m_result;
    bool m_done = false;
};

// One query at a time per session (plan 0021): `info show`, `help` and `<command> -h` of the GUI
// tab or the CLI console are queued here (at most maxQueued; a further one is answered at once with
// nullopt). A query waits until the shell is idle (ConsoleSession::shellBusyFor(), review P1), at
// most maxIdleWait; then, or when the session refuses it (busy, not connected), or when it does not
// finish, it answers nullopt. setSession() / clear() answer the pending ones with nullopt; a run()
// from such a callback is answered with nullopt too (review P4).
class ShellQueryQueue : public QObject {
    Q_OBJECT

public:
    static constexpr std::size_t maxQueued = 8;
    static constexpr std::chrono::milliseconds maxIdleWait{5000};
    using Callback = std::function<void(const std::optional<ShellQueryResult>& result)>;

    explicit ShellQueryQueue(std::function<bool()> busy = {}, QObject* parent = nullptr);
    ~ShellQueryQueue() override;
    ShellQueryQueue(const ShellQueryQueue&) = delete;
    ShellQueryQueue& operator=(const ShellQueryQueue&) = delete;
    ShellQueryQueue(ShellQueryQueue&&) = delete;
    ShellQueryQueue& operator=(ShellQueryQueue&&) = delete;

    // nullptr = no session (every query answers nullopt).
    void setSession(ConsoleSession* session);
    void run(std::string command, Callback done);
    void clear();
    [[nodiscard]] bool running() const noexcept { return static_cast<bool>(m_query); }

private:
    struct Pending {
        std::string command;
        Callback done;
    };
    enum class Turn : std::uint8_t { Ready, Wait, Expired };
    [[nodiscard]] Turn frontTurn();
    void next();
    [[nodiscard]] bool startQuery(Pending& item);
    void finishCurrent(const std::optional<ShellQueryResult>& result);
    void endQuery(const ShellQuery* query, const std::optional<ShellQueryResult>& result);

    std::function<bool()> m_busy;
    QPointer<ConsoleSession> m_session;
    std::deque<Pending> m_pending; // <= maxQueued
    std::unique_ptr<ShellQuery> m_query;
    Callback m_currentDone;
    QMetaObject::Connection m_sessionGone; // the running query's session went away
    QTimer m_idleTimer;                    // the front query waits for an idle shell
    std::chrono::steady_clock::time_point m_waitingSince;
    bool m_waiting = false;
    bool m_clearing = false;
};

// CLI helper: runs a local event loop until `query` finished (or its session went away) and
// returns the result; nullopt when it did not finish.
[[nodiscard]] std::optional<ShellQueryResult> waitForQuery(ShellQuery& query);

// CLI helper: runs `command` through `queue` and a local event loop until it is answered.
[[nodiscard]] std::optional<ShellQueryResult> runAndWait(ShellQueryQueue& queue,
                                                         std::string command);

} // namespace hio::device
