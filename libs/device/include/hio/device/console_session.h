#pragma once

// ConsoleSession - the console part shared by the J-Link DeviceSession and the BLE BleSession
// (ADR-0079, plan/features/0011-m5-ble-backend-cli.md). Consumers of a console (the CLI console
// and command, the GUI console page, later MCP) only use this interface.
//
// A QObject facade on the owner thread; each implementation has its own transport-specific worker
// (the J-Link session worker thread, ADR-0031; the shared BLE thread, ADR-0080). The signals are
// emitted on the owner thread in the order the worker produced the events.
//
// Thread safety: every member function must be called on the owner thread.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"

#include <QFlags>
#include <QList>
#include <QObject>
#include <QtTypes>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>

namespace hio::device {

// Who the session talks to (ADR-0079).
struct SessionIdentity {
    Transport transport = Transport::JLink;
    std::string id;          // J-Link: the serial number as decimal; BLE: the BleDeviceId
    std::string displayName; // "J-Link 801000001", "CHESTER-ABC123"
};

// What a transport can do besides the console, so the UI can hide the rest (ADR-0079).
enum class Capability : std::uint8_t {
    Reset = 1,      // reset the target from the console
    LogChannel = 2, // a separate device log channel
    Operations = 4, // flash, erase, recover, diagnose, ...
};
Q_DECLARE_FLAGS(Capabilities, Capability)

class ConsoleSession : public QObject {
    Q_OBJECT

public:
    ConsoleSession(const ConsoleSession&) = delete;
    ConsoleSession& operator=(const ConsoleSession&) = delete;
    ConsoleSession(ConsoleSession&&) = delete;
    ConsoleSession& operator=(ConsoleSession&&) = delete;
    ~ConsoleSession() override = default;

    // Queued for the worker; an Error only when the queue is full (internal.queue_full) or the
    // session is closed (cancelled). Results arrive as events (echo line, warning, connection).
    [[nodiscard]] virtual Status sendLine(std::string line) = 0;
    [[nodiscard]] virtual Status reconnect() = 0;
    // Stops the session and releases its registry slot and lock. Idempotent: calls closeSession()
    // once.
    void close();

    // Owner thread only (ADR-0063): the provided or the session's own log.
    [[nodiscard]] virtual const ConsoleLog& log() const = 0;
    // The last reported connection event (Connecting before the first one).
    [[nodiscard]] virtual const ConnectionEvent& connection() const = 0;
    [[nodiscard]] virtual SessionIdentity identity() const = 0;
    [[nodiscard]] virtual Capabilities capabilities() const = 0;
    [[nodiscard]] virtual const DeviceProfile& profile() const = 0;
    // The profile target whose console rules, history and file names the session uses ("app").
    [[nodiscard]] virtual std::string_view consoleTargetId() const = 0;
    // The console log file the session writes now; empty for none (plan 0005 rules).
    [[nodiscard]] virtual std::filesystem::path consoleFile() const = 0;

    // ---- silent shell captures (plan 0021 phase 2, ADR-0175: ShellQuery) ----
    // Returns true when it takes the line (it is then neither appended to the log nor emitted).
    using ShellCapture = std::function<bool(const ConsoleLine& line)>;
    static constexpr std::size_t maxHeldLines = 64;
    // The shell is idle (a query may start) after this much shell silence when the session has no
    // prompt (the ADR-0104 silence interval).
    static constexpr std::chrono::milliseconds shellQuietTime{300};
    // A sent line whose echo has not come back counts as in flight for at most this long (a write
    // that failed produces no echo).
    static constexpr std::chrono::milliseconds unechoedExpiry{2000};
    // With a prompt, an echoed line without a prompt after it counts as busy until the prompt or
    // this much shell silence (the ShellQuery timeout; user decision 2026-10-26).
    static constexpr std::chrono::milliseconds promptWaitExpiry{2000};
    // Installs (or, with an empty function, removes) the capture. While one is installed every
    // shell-channel line is offered to it first, and sendLine() holds the lines of other users
    // (at most maxHeldLines, then internal.queue_full); removing the capture sends the held lines
    // in order and emits shellCaptureEnded(). Owner thread only.
    void setShellCapture(ShellCapture capture);
    [[nodiscard]] bool shellCaptureActive() const noexcept { return static_cast<bool>(m_capture); }
    // A new origin token for sendCaptureLine() (taken before sending: an echo may come at once).
    [[nodiscard]] std::uint64_t newCaptureOrigin() noexcept { return ++m_nextOrigin; }
    // Sends the capture's own line past the hold; its echo carries `origin`
    // (ConsoleLine::origin).
    [[nodiscard]] Status sendCaptureLine(std::string line, std::uint64_t origin);
    // Plan 0021 review P1: how long the shell is still busy; 0 = idle. Busy while a line sent
    // through this session has no echo yet (at most unechoedExpiry), and with a prompt
    // (shellPromptOf) while an echoed line has no prompt after it (at most promptWaitExpiry of
    // shell silence), without one until
    // shellQuietTime passed since the last shell line or sent line. A non-zero result is the time
    // after which to ask again.
    [[nodiscard]] std::chrono::milliseconds shellBusyFor() const;

signals:
    void linesReceived(const QList<hio::device::ConsoleLine>& lines);
    void connectionChanged(const hio::device::ConnectionEvent& event);
    void linesDropped(quint64 count);
    void warning(const hio::Error& error);
    // The shell prompt was seen after output, when the profile has a prompt for this transport:
    // BLE BleProfile::shellPrompt (ADR-0082), J-Link RTT TargetProfile::shellPrompt (M6,
    // ADR-0104; empty for CHESTER until confirmed, open question 25). Never emitted without one.
    void promptSeen();
    // Plan 0021: a shell capture was removed (the held lines were sent).
    void shellCaptureEnded();

protected:
    ConsoleSession();

    // For the implementations (plan 0021): call for every received line before it is appended to
    // the log (it also tracks the shell activity of shellBusyFor()); true = the capture took it
    // (skip it).
    [[nodiscard]] bool captureLine(const ConsoleLine& line);
    // Call in sendLine() after the line was validated and before it is queued: true = the line is
    // held while a capture runs (`status` is then the result of sendLine()).
    [[nodiscard]] bool holdLine(std::string& line, Status& status);
    // The origin token for the line sendLine() queues now (0 unless sendCaptureLine() sends it).
    [[nodiscard]] std::uint64_t sendingOrigin() const noexcept { return m_sendingOrigin; }
    // Call after sendLine() queued a line (not held) for the transport.
    void lineQueued();
    [[nodiscard]] std::size_t heldLineCount() const noexcept { return m_held.size(); }

    // The concrete close. Each final class's destructor calls its own close path directly (no
    // virtual call from a destructor).
    virtual void closeSession() = 0;

private:
    bool m_closeCalled = false;
    ShellCapture m_capture;
    std::deque<std::string> m_held; // <= maxHeldLines
    std::uint64_t m_sendingOrigin = 0;
    std::uint64_t m_nextOrigin = 0;
    // Shell activity for shellBusyFor() (owner thread).
    std::size_t m_unechoed = 0;
    std::chrono::steady_clock::time_point m_lastSent;
    std::chrono::steady_clock::time_point m_lastShellActivity;
    bool m_promptPending = false;
};

// CLI helper (ADR-0053): runs a local QEventLoop until the first Connected (success), a
// Disconnected or Stopped event with an error (that error), a Stopped event without an error
// (cancelled: the session was stopped on purpose and does not connect by itself), `timeout`
// (timeout) or a stop request (cancelled).
[[nodiscard]] Status waitUntilConnected(ConsoleSession& session, std::chrono::milliseconds timeout,
                                        const std::stop_token& stop = {});

// The console with --auto-reconnect (rttt behaviour, decided 2026-10-09): waits only for the
// outcome of the first attempt. Success when it Connected or ended Disconnected (the session keeps
// retrying); a Stopped event ends it with its error (or cancelled without one); also timeout and
// cancelled (stop request) as in waitUntilConnected().
[[nodiscard]] Status waitForFirstAttempt(ConsoleSession& session, std::chrono::milliseconds timeout,
                                         const std::stop_token& stop = {});

} // namespace hio::device

Q_DECLARE_OPERATORS_FOR_FLAGS(hio::device::Capabilities)
