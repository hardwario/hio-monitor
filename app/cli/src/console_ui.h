#pragma once

// The console front end of `hio chester app console` and `hio ble console` (ADR-0033,
// plan/features/0002-m1-jlink-rtt.md; M5: any ConsoleSession, ADR-0079, plan 0011 C1).
//
// Interactive (stdin and stdout are terminals, no --json): a scrolling line mode with replxx -
// device output is printed above the prompt, Enter sends the line, Up/Down history, Ctrl+R
// search, hints from the history and Tab completion from the device command tree (plan 0021,
// device::Completer; both run on the input thread with its own copy of the history), F4 reconnect,
// Ctrl+D / Ctrl+Q / Ctrl+C on an empty line quit, Ctrl+C on a non-empty line clears it. replxx's
// input() runs on an owned thread; everything else (session signals, printing, history) runs on the
// main thread. Otherwise: plain line input from stdin (StdinLineReader); output as text or NDJSON
// (--json). End of input ends the session once the sent commands were written (at most 3 s) plus
// ConsoleUiOptions::linger (--linger, default 1 s) for their output.
//
// The session's events are shown from the moment ConsoleView is created (so --json streams the
// connecting/connected events too).

#include "cli_app.h"
#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/command_tree_learner.h"
#include "hio/device/console_line.h"
#include "hio/device/console_session.h"
#include "hio/device/session_events.h"

#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QtTypes>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace hio::cli {

enum class ConsoleStream : std::uint8_t {
    Out, // device output
    Err, // status, warnings
};

// Shows the events of one session (NDJSON with --json, text otherwise).
class ConsoleView {
public:
    using Printer = std::function<void(ConsoleStream, std::string_view)>;

    // `announceProbe`: print "Using J-Link ..." once the probe is known (J-Link sessions only).
    ConsoleView(const Context& context, device::ConsoleSession& session, bool colors,
                bool announceProbe);
    ~ConsoleView();
    ConsoleView(const ConsoleView&) = delete;
    ConsoleView& operator=(const ConsoleView&) = delete;
    ConsoleView(ConsoleView&&) = delete;
    ConsoleView& operator=(ConsoleView&&) = delete;

    // Human mode: where lines go (default: stdout / stderr). The interactive console prints
    // through replxx; `showEcho` = false hides the echo of sent commands (replxx shows them).
    void setPrinter(Printer printer, bool showEcho);
    void printStatus(std::string_view text); // human mode only
    // The final {"type":"connection","state":"stopped"} event (--json); skipped when the last
    // connection event printed was already Stopped (review R28).
    void printStopped();

    // Called once when a write to stdout failed (the reader of a pipe went away, review R32);
    // the console turns it into a stop request.
    void setOutputClosedHandler(std::function<void()> handler);
    [[nodiscard]] bool outputClosed() const noexcept { return m_outputClosed; }

    [[nodiscard]] std::uint64_t echoed() const noexcept { return m_echoed; }
    // A Stopped event with an error (fatal); the console ends with it.
    [[nodiscard]] const std::optional<Error>& fatalError() const noexcept { return m_fatal; }

private:
    void onLines(const QList<device::ConsoleLine>& lines);
    void onConnection(const device::ConnectionEvent& event);
    void onWarning(const Error& error);
    void onDropped(quint64 count);
    void print(ConsoleStream stream, std::string_view text);
    void printEvent(const QJsonValue& event);
    void checkOutput();
    void announceProbe();

    const Context* m_context;          // non-owning; outlives the view
    device::ConsoleSession* m_session; // non-owning; outlives the view
    bool m_colors;
    bool m_announceProbe;
    bool m_showEcho = true;
    Printer m_printer;
    std::uint64_t m_echoed = 0;
    std::optional<Error> m_fatal;
    bool m_stoppedPrinted = false; // --json: the last connection event printed was Stopped
    bool m_outputClosed = false;   // a write to stdout failed
    std::function<void()> m_onOutputClosed;
    QObject m_connections; // receiver context of the signal connections
};

struct ConsoleUiOptions {
    bool interactive = false;
    std::optional<std::filesystem::path> historyFile; // interactive only
    // Plan 0021 (ADR-0170), interactive only: replxx hints from the history (console/suggestions,
    // --no-suggestions) and Tab completion from the device command tree the learner learns on the
    // main thread (nullptr = none; it must outlive runConsole()).
    bool suggestions = false;
    device::CommandTreeLearner* learner = nullptr;
    std::string prompt = "uart:~$ "; // the local prompt (BLE: the profile's shell prompt)
    // Plain / --json mode: output is collected this long after the end of input (--linger).
    std::chrono::milliseconds linger{1000};
};

// Waits for the console's first connection: waitUntilConnected(), or with --auto-reconnect
// waitForFirstAttempt() (decided 2026-10-09), cancelled by context.stop. On failure (the
// session's error, timeout or cancelled) it closes the session and emits the --json console's one
// terminal "stopped" event, so the caller only prints the error envelope (review R34).
[[nodiscard]] Status waitForConsoleConnection(const Context& context,
                                              device::ConsoleSession& session, ConsoleView& view,
                                              bool autoReconnect,
                                              std::chrono::milliseconds timeout);

// Runs the console until the user quits, input ends, Ctrl+C / SIGTERM (cancelled) or a fatal
// session error (that error). The session must be connected already.
[[nodiscard]] Status runConsole(const Context& context, device::ConsoleSession& session,
                                ConsoleView& view, const ConsoleUiOptions& options);

} // namespace hio::cli
