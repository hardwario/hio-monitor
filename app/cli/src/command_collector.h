#pragma once

// Sending a command to a console session and collecting its response, shared by
// `hio chester app command` (M1) and `hio ble command` (plan 0011 C1, ADR-0085): every line is
// queued in order, the response is collected from the moment the command is sent, for `collect`
// counted from when the last line was written (its echo arrived); `"truncated"` when the
// CommandResponse bound was reached. For BLE the shell prompt after the last echo ends the
// collection early (ADR-0082); without it the timeout ends it, as for J-Link.

#include "cli_app.h"
#include "command_response.h"
#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/console_session.h"

#include <QList>
#include <QObject>

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace hio::cli {

// Collects the response from start() on.
class ResponseCollector {
public:
    ResponseCollector(const Context& context, device::ConsoleSession& session);

    void start() { m_collecting = true; }
    // Sent lines echoed by the session, i.e. written to the device.
    [[nodiscard]] std::size_t written() const { return m_written; }
    // The collected output and log lines, bounded (review R42).
    [[nodiscard]] const CommandResponse& response() const { return m_response; }
    // A connection error while collecting (e.g. the write timed out).
    [[nodiscard]] const std::optional<Error>& failure() const { return m_failure; }
    // The session left Connected while collecting (with or without an error).
    [[nodiscard]] bool disconnected() const { return m_disconnected; }
    // The shell prompt was seen after the last echo (BLE, ADR-0082).
    [[nodiscard]] bool promptAfterLastEcho() const { return m_promptAfterEcho; }

    // Warnings that mean a command line did not reach the device (J-Link and BLE link loss).
    [[nodiscard]] static bool endsTheCommand(ErrorCode code);

private:
    void collect(const QList<device::ConsoleLine>& lines);

    bool m_collecting = false;
    bool m_disconnected = false;
    bool m_promptAfterEcho = false;
    std::size_t m_written = 0;
    CommandResponse m_response;
    std::optional<Error> m_failure;
    QObject m_receiver;
};

// Queues the command lines in order; a full command queue keeps the rest for the next send()
// (review R39, as the console's LineSender).
class LineQueue {
public:
    LineQueue(device::ConsoleSession& session, const std::vector<std::string>& lines)
        : m_session(&session), m_lines(&lines) {}

    void send();
    // A send error other than a full queue (e.g. the session was closed).
    [[nodiscard]] const std::optional<Error>& error() const noexcept { return m_error; }

private:
    device::ConsoleSession* m_session;       // non-owning; outlives the queue
    const std::vector<std::string>* m_lines; // non-owning; outlives the queue
    std::size_t m_next = 0;
    std::optional<Error> m_error;
};

// Runs the event loop until `done` returns true or `duration` passed; cancelled on Ctrl+C /
// SIGTERM.
[[nodiscard]] Status runEventsUntil(const std::function<bool()>& done,
                                    std::chrono::milliseconds duration,
                                    const std::stop_token& stop);

struct CommandTiming {
    std::chrono::milliseconds writeTimeout; // per line, for the echoes (review R23)
    std::chrono::milliseconds collect;      // -t/--timeout, from the last echo
    bool endAtPrompt = false;               // BLE: the prompt after the last echo ends it
    // Not every line written within the write budget and the session still connected.
    ErrorCode writeTimeoutCode = ErrorCode::TargetRttWriteTimeout;
};

// Sends `lines` on a connected session and collects the response into `collector` (which was
// created before connecting). Errors: a send error, target.not_connected (the session
// disconnected before every line was written), timing.writeTimeoutCode, the session's failure,
// cancelled.
[[nodiscard]] Status runCommand(const Context& context, device::ConsoleSession& session,
                                ResponseCollector& collector, const std::vector<std::string>& lines,
                                const CommandTiming& timing);

// The lines of a CMD argument ("\r\n" or "\n").
[[nodiscard]] std::vector<std::string> splitLines(const std::string& text);

// data = {"command","output":[...],"log":[...],"truncated"}; humanText = the output lines.
[[nodiscard]] CommandResult commandResult(const Context& context, const std::string& command,
                                          const CommandResponse& response);

} // namespace hio::cli
