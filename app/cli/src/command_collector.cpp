#include "command_collector.h"

#include "cli_app.h"
#include "cli_log.h"
#include "command_response.h"
#include "console_events.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/console_session.h"
#include "hio/device/session_events.h"
#include "output.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace hio::cli {

namespace {

// Margin on top of lines x write timeout for the echoes (review R23).
constexpr std::chrono::milliseconds writeBudgetMargin{500};

} // namespace

ResponseCollector::ResponseCollector(const Context& context, device::ConsoleSession& session) {
    QObject::connect(&session, &device::ConsoleSession::linesReceived, &m_receiver,
                     [this](const QList<device::ConsoleLine>& lines) { collect(lines); });
    QObject::connect(&session, &device::ConsoleSession::connectionChanged, &m_receiver,
                     [this](const device::ConnectionEvent& event) {
                         if (m_collecting && event.error &&
                             event.state != device::ConnectionState::Connected) {
                             m_failure = event.error;
                         }
                         if (m_collecting && event.state != device::ConnectionState::Connected) {
                             m_disconnected = true; // no further line will be written
                         }
                     });
    QObject::connect(&session, &device::ConsoleSession::warning, &m_receiver,
                     [this, &context](const Error& error) {
                         qCWarning(lcCli).noquote() << toQString(error.message);
                         if (m_collecting && endsTheCommand(error.code)) {
                             m_failure = error; // the command was not written (review R16)
                         } else if (context.mode == OutputMode::Human) {
                             std::cerr << humanWarning(error) << '\n';
                         }
                     });
    QObject::connect(&session, &device::ConsoleSession::promptSeen, &m_receiver, [this] {
        if (m_collecting && m_written > 0) {
            m_promptAfterEcho = true;
        }
    });
}

void ResponseCollector::collect(const QList<device::ConsoleLine>& lines) {
    for (const device::ConsoleLine& line : lines) {
        if (m_collecting && line.dir == device::Direction::In) {
            ++m_written;
            m_promptAfterEcho = false; // the prompt must follow the last echo
        }
        if (m_collecting && line.dir == device::Direction::Out) {
            // Beyond the bound the first lines are kept and nothing more is collected.
            if (line.channel == device::Channel::Log) {
                m_response.addLog(line.text);
            } else {
                m_response.addOutput(line.text);
            }
        }
    }
}

bool ResponseCollector::endsTheCommand(ErrorCode code) {
    switch (code) {
    case ErrorCode::TargetNotConnected:
    case ErrorCode::TargetRttBufferNotFound:
    case ErrorCode::TargetRttWriteTimeout:
    case ErrorCode::TargetRttWriteFailed:
    // M5 (plan 0011 C1): the BLE link-loss codes.
    case ErrorCode::BleWriteFailed:
    case ErrorCode::BleWriteTimeout:
    case ErrorCode::BleDisconnected:
    case ErrorCode::BleNotPaired:
        return true;
    default:
        return false;
    }
}

void LineQueue::send() {
    while (m_next < m_lines->size() && !m_error) {
        Status sent = m_session->sendLine(m_lines->at(m_next));
        if (!sent && sent.error().code == ErrorCode::InternalQueueFull) {
            return; // the worker is busy writing; the rest goes in the next round
        }
        if (!sent) {
            m_error = std::move(sent).error();
            return;
        }
        ++m_next;
    }
}

Status runEventsUntil(const std::function<bool()>& done, std::chrono::milliseconds duration,
                      const std::stop_token& stop) {
    Status result;
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QTimer stopPoll;
    QObject::connect(&stopPoll, &QTimer::timeout, &loop, [&] {
        if (stop.stop_requested()) {
            result = fail(ErrorCode::Cancelled);
            loop.quit();
        } else if (done()) {
            loop.quit();
        }
    });
    deadline.start(duration);
    stopPoll.start(std::chrono::milliseconds(20));
    loop.exec();
    return result;
}

Status runCommand(const Context& context, device::ConsoleSession& session,
                  ResponseCollector& collector, const std::vector<std::string>& lines,
                  const CommandTiming& timing) {
    // py-hardwario: send every line, then collect for the timeout counted from when the last
    // line was written (its echo arrived). The lines are queued while the event loop runs, a full
    // command queue (more than 256 lines) keeps the rest for the next round (review R39, as the
    // console's LineSender). The worker writes them one after another, each within the write
    // timeout, so the echoes are awaited for one write timeout per line, counted as the lines are
    // written (line n+1 until start + (n + 1) x write timeout + 500 ms, n = lines written so
    // far; at most lines x write timeout + 500 ms in total), or until the worker reports a failed
    // write or the session leaves Connected; the command takes at most that + timeout (review
    // fix 7, R23).
    collector.start();
    const auto writeBudget =
        (timing.writeTimeout * static_cast<std::int64_t>(lines.size())) + writeBudgetMargin;
    const auto started = std::chrono::steady_clock::now();
    LineQueue queue(session, lines);
    queue.send();
    Status collected = runEventsUntil(
        [&] {
            queue.send();
            const auto lineBudget =
                (timing.writeTimeout * static_cast<std::int64_t>(collector.written() + 1)) +
                writeBudgetMargin;
            return queue.error().has_value() || collector.written() >= lines.size() ||
                   collector.failure().has_value() || collector.disconnected() ||
                   std::chrono::steady_clock::now() - started >= lineBudget;
        },
        writeBudget, context.stop);
    if (queue.error()) {
        return std::unexpected(*queue.error());
    }
    if (collected && !collector.failure() && collector.written() < lines.size()) {
        // Not every line was written (review R16): the session disconnected, or the worker
        // could not complete the writes within the budget.
        const std::string detail =
            std::format("{} of {} command lines written", collector.written(), lines.size());
        return std::unexpected(collector.disconnected()
                                   ? makeError(ErrorCode::TargetNotConnected, detail)
                                   : makeError(timing.writeTimeoutCode, detail));
    }
    if (collected && !collector.failure()) {
        // The collection ends early when the response bound is reached (review R42) or, for
        // BLE, at the prompt after the last echo (ADR-0082).
        collected = runEventsUntil(
            [&collector, &timing] {
                return collector.response().truncated() ||
                       (timing.endAtPrompt && collector.promptAfterLastEcho());
            },
            timing.collect, context.stop);
    }
    if (!collected) {
        return collected;
    }
    if (collector.failure()) {
        return std::unexpected(*collector.failure());
    }
    return {};
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);) {
        if (line.ends_with('\r')) {
            line.pop_back();
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

CommandResult commandResult(const Context& context, const std::string& command,
                            const CommandResponse& response) {
    QJsonArray outputJson;
    std::string text;
    for (const std::string& line : response.output()) {
        outputJson.append(toQString(line));
        text += text.empty() ? line : "\n" + line;
    }
    QJsonArray logJson;
    for (const std::string& line : response.log()) {
        logJson.append(toQString(line));
    }
    QJsonObject data;
    data.insert(QStringLiteral("command"), toQString(command));
    data.insert(QStringLiteral("output"), outputJson);
    data.insert(QStringLiteral("log"), logJson);
    data.insert(QStringLiteral("truncated"), response.truncated());
    if (response.truncated()) {
        qCWarning(lcCli) << "Command output truncated after" << response.lines() << "lines,"
                         << response.bytes() << "bytes";
        if (context.mode == OutputMode::Human) {
            std::cerr << std::format("Output truncated after {} lines\n", response.lines());
        }
    }
    return CommandResult{.data = data, .humanText = text, .streamed = false};
}

} // namespace hio::cli
