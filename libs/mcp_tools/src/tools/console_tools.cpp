// Console tools (plan/features/0013 C1, ADR-0104, ADR-0105, ADR-0155): send_command,
// read_terminal, read_log, wait_for_state, wait_for_connection.

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/console_session.h"
#include "hio/device/session_events.h"
#include "hio/mcp/limits.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/schema.h"
#include "hio/mcp/tool_call.h"
#include "hio/mcp/tools/device_provider.h"
#include "hio/mcp/tools/tools_context.h"
#include "scan_scheduler.h"
#include "tool_support.h"
#include "tools/tools.h"

#include <QChar>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QTimer>
#include <QtTypes>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::mcp::tools::detail {

namespace {

using hio::device::Channel;
using hio::device::ChannelMask;
using hio::device::ConsoleLine;
using hio::device::Direction;
using hio::mcp::Required;
using hio::mcp::Schema;

constexpr const char* sendCommandDescription =
    "Send one shell command to the device and return its output. Output belongs to this command "
    "only: it starts after the command's echo. The call ends when the shell prompt appears "
    "(end=\"prompt\"; Bluetooth shell and profiles with a known RTT prompt), else after 300 ms "
    "without new output once output has started (end=\"silence\"), and always at `timeout` "
    "seconds (end=\"timeout\", not an error; increase it for slow commands). interleaved=true "
    "means another command (typed by the user or another call) was sent meanwhile; its echo is "
    "in the output as dir \"in\". Device logs are separate: pass log_cursor to read_log as "
    "after_cursor to read the logs this command caused. Output is capped at 2000 lines / "
    "256 KiB (truncated=true). Try `help`, `version` or `kernel uptime`.";

constexpr const char* readTerminalDescription =
    "Read shell (terminal) lines of a device: dir \"out\" = device output, \"in\" = commands "
    "sent, \"marker\" = notes written by HARDWARIO Monitor (e.g. AI actions). Without "
    "after_cursor the newest `lines` lines; with after_cursor the lines after it (oldest first, "
    "at most `lines`), so pass cursor_end to the next call to continue. dropped_before counts "
    "lines lost before the first returned line because the buffer overflowed. At most 5000 "
    "lines / 1 MiB per call.";

constexpr const char* readLogDescription =
    "Read device log lines (Zephyr log, with level dbg/inf/wrn/err). The log is kept in a ring "
    "buffer and can be read repeatedly; the buffer is not cleared by flashing. lines=-1 (default) "
    "returns all buffered lines (the newest 5000 at most), lines=N the newest N. With "
    "after_cursor (from send_command, wait_for_state, flash, reset, ... or cursor_end of an "
    "earlier read_log) only lines after it are returned, oldest first; continue with cursor_end. "
    "pattern is a case-insensitive regular expression (at most 512 characters) that lines must "
    "contain (only the first 4096 bytes of a line are matched). A pattern scans at most 20000 "
    "lines per call: with after_cursor the call then ends early with truncated=true and "
    "cursor_end = the last scanned line, so continue with after_cursor=cursor_end; without "
    "after_cursor only the newest 20000 lines are searched (truncated=true when older lines "
    "exist; use after_cursor=0 to search them all). dropped_before counts lines lost before the "
    "first returned line because the buffer overflowed. At most 5000 lines / 1 MiB per call "
    "(truncated=true).";

constexpr const char* waitForStateDescription =
    "Wait until a line matching `pattern` (case-insensitive regular expression) appears in the "
    "device output, or until `timeout` seconds pass (at most 600). Only lines that arrive after "
    "the call starts are matched. Passive (no command): just watch. Polling (command set): send "
    "the command at once and every poll_interval seconds and match the lines that follow - for "
    "state queries such as `lte state` that must be repeated until the answer changes. source: "
    "\"log\" (device logs, default), \"terminal\" (shell output) or \"both\". A timeout is not an "
    "error: the result has status \"timeout\", matched=false and the last lines seen "
    "(last_lines). Example: after flash, wait_for_state(pattern=\"attached: yes\", "
    "command=\"lte state\", timeout=180).";

constexpr const char* waitForConnectionDescription =
    "Wait until the device's console is connected (at most `timeout` seconds, up to 600). Use it "
    "after anything that takes the device away for a while - flashing, a reset, a power cycle, "
    "reconnect, reacquire_probe - instead of polling status. On timeout (status \"timeout\", not "
    "an error) the result says which state it is stuck in and why (state, error).";

[[nodiscard]] std::chrono::milliseconds secondsArgument(const QJsonObject& args, const char* name,
                                                        std::chrono::milliseconds max) {
    const double value = args.value(QString::fromLatin1(name)).toDouble();
    const auto ms =
        std::chrono::milliseconds(static_cast<std::int64_t>(std::llround(value * 1000)));
    return std::min(ms, max);
}

[[nodiscard]] std::optional<QRegularExpression> compilePattern(const QString& pattern) {
    QRegularExpression regex(pattern, QRegularExpression::CaseInsensitiveOption);
    if (!regex.isValid()) {
        return std::nullopt;
    }
    regex.optimize();
    return regex;
}

std::optional<hio::mcp::ValidationError>
checkPattern(const QJsonObject& args, const hio::mcp::Limits& limits, bool required) {
    if (!args.contains(QStringLiteral("pattern"))) {
        return required ? invalid(QStringLiteral("pattern"), QStringLiteral("is required"))
                        : std::nullopt;
    }
    const QString pattern = args.value(QStringLiteral("pattern")).toString();
    if (std::cmp_greater(pattern.size(), limits.tools.regexMaxChars)) {
        return invalid(
            QStringLiteral("pattern"),
            QStringLiteral("is longer than %1 characters").arg(limits.tools.regexMaxChars));
    }
    QRegularExpression regex(pattern, QRegularExpression::CaseInsensitiveOption);
    if (!regex.isValid()) {
        return invalid(QStringLiteral("pattern"),
                       QStringLiteral("is not a valid regular expression: %1 (offset %2)")
                           .arg(regex.errorString(), QString::number(regex.patternErrorOffset())));
    }
    return std::nullopt;
}

// ---- reading ------------------------------------------------------------------------------------

struct ReadOutcome {
    std::vector<ConsoleLine> lines;
    std::uint64_t cursorEnd = 0;
    std::uint64_t droppedBefore = 0;
    bool truncated = false;
};

// ADR-0155: lines of `channel` with seq > cursor that are no longer stored. The log has one seq
// across both channels, so the gap below firstSeq may also hold lines of the other channel: the
// value is exact for a channel that received all lines of the gap and an upper bound otherwise
// (capped by the lines the channel ever lost).
[[nodiscard]] std::uint64_t droppedBefore(const hio::device::ConsoleLog& log, Channel channel,
                                          std::uint64_t cursor) {
    const std::uint64_t gapEnd = log.firstSeq(channel).value_or(log.lastSeq() + 1);
    if (cursor + 1 >= gapEnd) {
        return 0;
    }
    const std::uint64_t lost = log.total(channel) - log.size(channel);
    return std::min(gapEnd - cursor - 1, lost);
}

// Lines of one channel admitted under the caller's count and the per-call bounds.
class LineBudget {
public:
    enum class Verdict : std::uint8_t { Take, Enough, Bounded };

    LineBudget(std::int64_t count, const hio::mcp::ToolLimits& limits)
        : m_wanted(count >= 0 ? static_cast<std::size_t>(count)
                              : std::numeric_limits<std::size_t>::max()),
          m_maxLines(limits.readMaxLines), m_maxBytes(limits.readMaxBytes) {}

    // `count` (the caller's choice; -1 = all) ends a read quietly (Enough); the per-call bounds
    // (readMaxLines / readMaxBytes) end it with truncated = true (Bounded).
    [[nodiscard]] Verdict admit(const ConsoleLine& line) {
        if (m_taken >= m_wanted) {
            return Verdict::Enough;
        }
        if (m_taken >= m_maxLines || m_bytes + line.text.size() > m_maxBytes) {
            return Verdict::Bounded;
        }
        ++m_taken;
        m_bytes += line.text.size();
        return Verdict::Take;
    }
    // Lines to fetch for a read of the newest lines without a pattern.
    [[nodiscard]] std::size_t newestFetch(std::size_t stored) const {
        return std::min({m_wanted, m_maxLines + 1, stored});
    }

private:
    std::size_t m_wanted;
    std::size_t m_maxLines;
    std::size_t m_maxBytes;
    std::size_t m_taken = 0;
    std::size_t m_bytes = 0;
};

// Review R10: only the first `maxBytes` bytes of a line are matched (bounded regex work per line).
[[nodiscard]] bool matchesText(const QRegularExpression& regex, const std::string& text,
                               std::size_t maxBytes) {
    const std::string_view head = std::string_view(text).substr(0, maxBytes);
    return regex.match(QString::fromUtf8(head.data(), static_cast<qsizetype>(head.size())))
        .hasMatch();
}

[[nodiscard]] bool matches(const std::optional<QRegularExpression>& regex, const ConsoleLine& line,
                           std::size_t maxBytes) {
    return !regex || matchesText(*regex, line.text, maxBytes);
}

[[nodiscard]] ChannelMask maskOf(Channel channel) {
    return channel == Channel::Shell ? ChannelMask::Shell : ChannelMask::Log;
}

// Forward from the cursor, oldest first, in bounded chunks. With a pattern at most
// readScanMaxLines lines are scanned (review R10); then cursorEnd is the last scanned line.
[[nodiscard]] ReadOutcome readAfter(const hio::device::ConsoleLog& log, Channel channel,
                                    std::uint64_t cursor, LineBudget budget,
                                    const std::optional<QRegularExpression>& regex,
                                    const hio::mcp::ToolLimits& limits) {
    ReadOutcome outcome;
    outcome.droppedBefore = droppedBefore(log, channel, cursor);
    std::uint64_t position = cursor;
    std::size_t scanned = 0;
    constexpr std::size_t chunk = 1024;
    bool more = true;
    while (more) {
        const std::vector<ConsoleLine> lines = log.linesSince(position, maskOf(channel), chunk);
        more = lines.size() == chunk;
        for (const ConsoleLine& line : lines) {
            if (regex && scanned >= limits.readScanMaxLines) {
                outcome.truncated = true; // `line` and later ones were not scanned
                outcome.cursorEnd = position;
                return outcome;
            }
            ++scanned;
            if (!matches(regex, line, limits.regexMaxLineBytes)) {
                position = line.seq;
                continue;
            }
            if (const auto verdict = budget.admit(line); verdict != LineBudget::Verdict::Take) {
                outcome.truncated = verdict == LineBudget::Verdict::Bounded;
                outcome.cursorEnd = outcome.lines.empty() ? position : outcome.lines.back().seq;
                return outcome;
            }
            outcome.lines.push_back(line);
            position = line.seq;
        }
    }
    outcome.cursorEnd = std::max(cursor, log.lastSeq());
    return outcome;
}

// The newest lines. A pattern filters the newest readScanMaxLines buffered lines (review R10;
// truncated when older lines were not searched).
[[nodiscard]] ReadOutcome readNewest(const hio::device::ConsoleLog& log, Channel channel,
                                     LineBudget budget,
                                     const std::optional<QRegularExpression>& regex,
                                     const hio::mcp::ToolLimits& limits) {
    ReadOutcome outcome;
    outcome.cursorEnd = log.lastSeq();
    const std::size_t stored = log.size(channel);
    const std::size_t fetch =
        regex ? std::min(stored, limits.readScanMaxLines) : budget.newestFetch(stored);
    outcome.truncated = regex && fetch < stored;
    std::vector<ConsoleLine> lines = log.last(fetch, maskOf(channel));
    for (ConsoleLine& line : std::ranges::reverse_view(lines)) {
        if (!matches(regex, line, limits.regexMaxLineBytes)) {
            continue;
        }
        if (const auto verdict = budget.admit(line); verdict != LineBudget::Verdict::Take) {
            outcome.truncated = outcome.truncated || verdict == LineBudget::Verdict::Bounded;
            break;
        }
        outcome.lines.push_back(std::move(line));
    }
    std::ranges::reverse(outcome.lines);
    return outcome;
}

[[nodiscard]] ReadOutcome readLines(const hio::device::ConsoleLog& log, Channel channel,
                                    std::optional<std::uint64_t> cursor, std::int64_t count,
                                    const std::optional<QRegularExpression>& regex,
                                    const hio::mcp::ToolLimits& limits) {
    const LineBudget budget(count, limits);
    return cursor ? readAfter(log, channel, *cursor, budget, regex, limits)
                  : readNewest(log, channel, budget, regex, limits);
}

QJsonObject readResult(const ReadOutcome& outcome, bool logLines, const DeviceRef& ref) {
    QJsonArray lines;
    for (const ConsoleLine& line : outcome.lines) {
        lines.append(logLines ? logLineJson(line) : terminalLineJson(line));
    }
    QJsonObject result;
    result.insert(QStringLiteral("lines"), lines);
    result.insert(QStringLiteral("cursor_end"), seqJson(outcome.cursorEnd));
    result.insert(QStringLiteral("dropped_before"), seqJson(outcome.droppedBefore));
    result.insert(QStringLiteral("truncated"), outcome.truncated);
    result.insert(QStringLiteral("paused_by_operation"), pausedByOperation(ref));
    return result;
}

QJsonObject readOutputSchema(bool logLines) {
    const QJsonObject line = logLines ? objectOf({{"seq", typed("integer")},
                                                  {"level", nullable("string")},
                                                  {"text", typed("string")},
                                                  {"time", typed("string")}})
                                      : objectOf({{"seq", typed("integer")},
                                                  {"dir", typed("string")},
                                                  {"text", typed("string")},
                                                  {"time", typed("string")}});
    return objectOf({{"lines", arrayOf(line)},
                     {"cursor_end", typed("integer")},
                     {"dropped_before", typed("integer")},
                     {"truncated", typed("boolean")},
                     {"paused_by_operation", typed("boolean")}},
                    {QStringLiteral("lines"), QStringLiteral("cursor_end")});
}

// ---- send_command (ADR-0104, ADR-0105) ----------------------------------------------------------

class SendCommandCall final : public DeviceCall {
public:
    SendCommandCall(ToolsContext& tools, const hio::mcp::RequestContext& context,
                    hio::mcp::ToolEnv& env, ResolvedDevice device, QString command,
                    std::chrono::milliseconds timeout)
        : DeviceCall(tools, context, env, std::move(device)), m_command(std::move(command)),
          m_commandUtf8(m_command.toStdString()),
          m_promptMode(!shellPromptOf(*ref().console).empty()), m_hint(true) {
        const hio::device::ConsoleSession& session = *ref().console;
        provider().marker(id(), markerStart(this->context(), QStringLiteral("send_command")));
        provider().setAiCommandHint(id(), true);
        m_start = session.log().lastSeq();
        connect(ref().console.data(), &hio::device::ConsoleSession::linesReceived, this,
                [this](const QList<ConsoleLine>& lines) { onLines(lines); });
        connect(ref().console.data(), &hio::device::ConsoleSession::promptSeen, this,
                [this] { onPrompt(); });
        m_deadline.setSingleShot(true);
        m_deadline.setInterval(timeout);
        connect(&m_deadline, &QTimer::timeout, this, [this] { end(QStringLiteral("timeout")); });
        m_silence.setSingleShot(true);
        m_silence.setInterval(limits().sendCommandSilence);
        connect(&m_silence, &QTimer::timeout, this, [this] { end(QStringLiteral("silence")); });
        if (auto sent = ref().console->sendLine(m_commandUtf8); !sent) {
            clearHint();
            completeError(std::move(sent).error());
            return;
        }
        m_deadline.start();
    }

    ~SendCommandCall() override { clearHint(); }
    SendCommandCall(const SendCommandCall&) = delete;
    SendCommandCall& operator=(const SendCommandCall&) = delete;
    SendCommandCall(SendCommandCall&&) = delete;
    SendCommandCall& operator=(SendCommandCall&&) = delete;

protected:
    void onCancel() override {
        stopTimers();
        clearHint();
        ToolCall::onCancel();
    }
    void deviceGone() override {
        stopTimers();
        m_hint = false;
    }

private:
    void onLines(const QList<ConsoleLine>& lines) {
        if (isFinished()) {
            return;
        }
        bool output = false;
        for (const ConsoleLine& line : lines) {
            if (line.seq <= m_start || line.channel != Channel::Shell) {
                continue;
            }
            if (!m_echoSeen) {
                // Output before the echo belongs to an earlier command (ADR-0104).
                m_echoSeen = line.dir == Direction::In && line.text == m_commandUtf8;
                continue;
            }
            if (line.dir == Direction::Marker) {
                continue;
            }
            if (line.dir == Direction::In) {
                m_interleaved = true; // ADR-0105: another command was sent meanwhile
                append(line);
                continue;
            }
            // A shell that echoes the typed line itself: drop that copy (first line only).
            if (!m_outputStarted && line.text == m_commandUtf8) {
                m_outputStarted = true;
                continue;
            }
            m_outputStarted = true;
            output = true;
            append(line);
        }
        if (output && !m_promptMode) {
            m_silence.start();
        }
    }

    void append(const ConsoleLine& line) {
        if (m_output.size() >= static_cast<qsizetype>(limits().sendCommandMaxLines) ||
            m_bytes + line.text.size() > limits().sendCommandMaxBytes) {
            m_truncated = true;
            return;
        }
        m_bytes += line.text.size();
        QJsonObject item;
        item.insert(QStringLiteral("dir"), qs(hio::device::directionId(line.dir)));
        item.insert(QStringLiteral("text"), qs(line.text));
        m_output.append(item);
    }

    void onPrompt() {
        if (m_echoSeen && m_promptMode) {
            end(QStringLiteral("prompt"));
        }
    }

    void end(const QString& reason) {
        if (isFinished()) {
            return;
        }
        stopTimers();
        clearHint();
        if (!alive()) {
            completeError(deviceNotOpen(id()));
            return;
        }
        const std::uint64_t cursor = ref().console->log().lastSeq();
        QJsonObject result;
        result.insert(QStringLiteral("command"), m_command);
        result.insert(QStringLiteral("output"), m_output);
        result.insert(QStringLiteral("end"), reason);
        result.insert(QStringLiteral("interleaved"), m_interleaved);
        result.insert(QStringLiteral("log_cursor"), seqJson(m_start));
        result.insert(QStringLiteral("log_cursor_end"), seqJson(cursor));
        result.insert(QStringLiteral("terminal_cursor_end"), seqJson(cursor));
        result.insert(QStringLiteral("truncated"), m_truncated);
        result.insert(QStringLiteral("paused_by_operation"), pausedByOperation(ref()));
        complete(result);
    }

    void stopTimers() {
        m_deadline.stop();
        m_silence.stop();
    }

    void clearHint() {
        if (m_hint) {
            m_hint = false;
            provider().setAiCommandHint(id(), false);
        }
    }

    QString m_command;
    std::string m_commandUtf8;
    QTimer m_deadline;
    QTimer m_silence;
    QJsonArray m_output;
    std::size_t m_bytes = 0;
    std::uint64_t m_start = 0;
    bool m_promptMode = false;
    bool m_echoSeen = false;
    bool m_outputStarted = false;
    bool m_interleaved = false;
    bool m_truncated = false;
    bool m_hint = false;
};

// ---- wait_for_state -----------------------------------------------------------------------------

class WaitForStateCall final : public DeviceCall, public LineScanner {
public:
    static constexpr std::size_t lastLinesKept = 10;

    WaitForStateCall(ToolsContext& tools, const hio::mcp::RequestContext& context,
                     hio::mcp::ToolEnv& env, ResolvedDevice device, QRegularExpression regex,
                     ChannelMask mask, std::chrono::milliseconds timeout, QString command,
                     std::chrono::milliseconds pollInterval)
        : DeviceCall(tools, context, env, std::move(device)), m_regex(std::move(regex)),
          m_mask(mask), m_timeout(timeout), m_command(std::move(command)),
          m_maxLineBytes(limits().regexMaxLineBytes) {
        m_elapsed.start();
        if (!m_command.isEmpty()) {
            provider().marker(id(),
                              markerStart(this->context(),
                                          QStringLiteral("wait_for_state \"%1\"").arg(m_command)));
            provider().setAiCommandHint(id(), true);
            m_hint = true;
        }
        m_cursor = ref().console->log().lastSeq();
        connect(ref().console.data(), &hio::device::ConsoleSession::linesReceived, this,
                [this](const QList<ConsoleLine>&) {
                    if (!isFinished()) {
                        this->tools().scanScheduler().request(*this);
                    }
                });
        m_deadline.setSingleShot(true);
        m_deadline.setInterval(timeout);
        connect(&m_deadline, &QTimer::timeout, this, [this] { timedOut(); });
        m_progress.setInterval(std::chrono::seconds(1));
        connect(&m_progress, &QTimer::timeout, this, [this] {
            reportProgress(seconds(elapsed()), seconds(m_timeout), QStringLiteral("waiting"));
        });
        if (!m_command.isEmpty()) {
            m_poll.setInterval(pollInterval);
            connect(&m_poll, &QTimer::timeout, this, [this] { sendCommand(); });
            m_poll.start();
            sendCommand();
            if (isFinished()) {
                return;
            }
        }
        m_deadline.start();
        m_progress.start();
    }

    ~WaitForStateCall() override {
        tools().scanScheduler().remove(*this);
        clearHint();
    }
    WaitForStateCall(const WaitForStateCall&) = delete;
    WaitForStateCall& operator=(const WaitForStateCall&) = delete;
    WaitForStateCall(WaitForStateCall&&) = delete;
    WaitForStateCall& operator=(WaitForStateCall&&) = delete;

    bool scanSome(std::size_t maxLines) override {
        if (isFinished()) {
            return false;
        }
        if (!alive()) {
            if (m_timedOut) {
                finishTimeout();
            }
            return false;
        }
        const std::vector<ConsoleLine> lines =
            ref().console->log().linesSince(m_cursor, m_mask, maxLines);
        for (const ConsoleLine& line : lines) {
            if (m_timedOut && line.seq > m_timeoutSeq) {
                finishTimeout(); // only lines that arrived before the timeout count
                return false;
            }
            m_cursor = line.seq;
            if (line.dir != Direction::Out) {
                continue; // commands and markers are not device output
            }
            m_last.push_back(line.text);
            if (m_last.size() > lastLinesKept) {
                m_last.pop_front();
            }
            if (matchesText(m_regex, line.text, m_maxLineBytes)) {
                matched(line);
                return false;
            }
        }
        if (lines.size() == maxLines) {
            return true;
        }
        if (m_timedOut) {
            finishTimeout(); // the backlog of the timeout is scanned
        }
        return false;
    }

protected:
    void onCancel() override {
        stop();
        ToolCall::onCancel();
    }
    void deviceGone() override {
        m_hint = false;
        stop();
    }

private:
    [[nodiscard]] std::chrono::milliseconds elapsed() const {
        return std::chrono::milliseconds(m_elapsed.elapsed());
    }

    void sendCommand() {
        if (isFinished() || !alive()) {
            return;
        }
        if (auto sent = ref().console->sendLine(m_command.toStdString()); !sent) {
            stop();
            completeError(std::move(sent).error());
        }
    }

    void matched(const ConsoleLine& line) {
        stop();
        QJsonObject result = common(QStringLiteral("ok"), true);
        result.insert(QStringLiteral("matched_line"), qs(line.text));
        complete(result);
    }

    // Lines that arrived but were not scanned yet still count. Review R10: the backlog is not
    // drained here in one go; the scheduler scans it in bounded turns (lines up to the timeout
    // only) and scanSome() finishes the call.
    void timedOut() {
        if (isFinished() || m_timedOut) {
            return;
        }
        m_timedOut = true;
        m_deadline.stop();
        m_poll.stop();
        m_progress.stop();
        m_timeoutSeq = alive() ? ref().console->log().lastSeq() : m_cursor;
        tools().scanScheduler().request(*this);
    }

    void finishTimeout() {
        if (isFinished()) {
            return;
        }
        if (!alive()) {
            stop();
            completeError(deviceNotOpen(id()));
            return;
        }
        stop();
        QJsonObject result = common(QStringLiteral("timeout"), false);
        QJsonArray last;
        for (const std::string& text : m_last) {
            last.append(qs(text));
        }
        result.insert(QStringLiteral("last_lines"), last);
        complete(result);
    }

    [[nodiscard]] QJsonObject common(const QString& status, bool matchedFlag) const {
        QJsonObject result;
        result.insert(QStringLiteral("status"), status);
        result.insert(QStringLiteral("matched"), matchedFlag);
        result.insert(QStringLiteral("pattern"), m_regex.pattern());
        result.insert(QStringLiteral("elapsed"), seconds(elapsed()));
        const std::uint64_t cursor = alive() ? ref().console->log().lastSeq() : m_cursor;
        result.insert(QStringLiteral("log_cursor_end"), seqJson(cursor));
        result.insert(QStringLiteral("terminal_cursor_end"), seqJson(cursor));
        result.insert(QStringLiteral("paused_by_operation"), pausedByOperation(ref()));
        return result;
    }

    void stop() {
        m_deadline.stop();
        m_poll.stop();
        m_progress.stop();
        tools().scanScheduler().remove(*this);
        clearHint();
    }

    void clearHint() {
        if (m_hint) {
            m_hint = false;
            provider().setAiCommandHint(id(), false);
        }
    }

    QRegularExpression m_regex;
    ChannelMask m_mask;
    std::chrono::milliseconds m_timeout;
    QString m_command;
    QElapsedTimer m_elapsed;
    QTimer m_deadline;
    QTimer m_poll;
    QTimer m_progress;
    std::uint64_t m_cursor = 0;
    std::uint64_t m_timeoutSeq = 0; // the last line that counts after a timeout
    std::size_t m_maxLineBytes;     // ToolLimits::regexMaxLineBytes
    std::deque<std::string> m_last; // <= lastLinesKept
    bool m_hint = false;
    bool m_timedOut = false; // the deadline passed; the backlog is being scanned
};

// ---- wait_for_connection ------------------------------------------------------------------------

class WaitForConnectionCall final : public DeviceCall {
public:
    WaitForConnectionCall(ToolsContext& tools, const hio::mcp::RequestContext& context,
                          hio::mcp::ToolEnv& env, ResolvedDevice device,
                          std::chrono::milliseconds timeout)
        : DeviceCall(tools, context, env, std::move(device)), m_timeout(timeout) {
        m_elapsed.start();
        if (ref().console->connection().state == hio::device::ConnectionState::Connected) {
            finishWith(true);
            return;
        }
        connect(ref().console.data(), &hio::device::ConsoleSession::connectionChanged, this,
                [this](const hio::device::ConnectionEvent& event) {
                    if (event.state == hio::device::ConnectionState::Connected) {
                        finishWith(true);
                    }
                });
        m_deadline.setSingleShot(true);
        m_deadline.setInterval(timeout);
        connect(&m_deadline, &QTimer::timeout, this, [this] { finishWith(false); });
        m_progress.setInterval(std::chrono::seconds(1));
        connect(&m_progress, &QTimer::timeout, this, [this] {
            reportProgress(seconds(std::chrono::milliseconds(m_elapsed.elapsed())),
                           seconds(m_timeout), QStringLiteral("waiting"));
        });
        m_deadline.start();
        m_progress.start();
    }

protected:
    void onCancel() override {
        m_deadline.stop();
        m_progress.stop();
        ToolCall::onCancel();
    }
    void deviceGone() override {
        m_deadline.stop();
        m_progress.stop();
    }

private:
    void finishWith(bool connected) {
        if (isFinished()) {
            return;
        }
        m_deadline.stop();
        m_progress.stop();
        if (!alive()) {
            completeError(deviceNotOpen(id()));
            return;
        }
        const hio::device::ConnectionEvent& event = ref().console->connection();
        QJsonObject result;
        result.insert(QStringLiteral("status"),
                      connected ? QStringLiteral("ok") : QStringLiteral("timeout"));
        result.insert(QStringLiteral("connected"), connected);
        result.insert(QStringLiteral("state"), qs(hio::device::connectionStateId(event.state)));
        result.insert(QStringLiteral("error"), event.error ? QJsonValue(hio::toJson(*event.error))
                                                           : QJsonValue(QJsonValue::Null));
        result.insert(QStringLiteral("elapsed"),
                      seconds(std::chrono::milliseconds(m_elapsed.elapsed())));
        complete(result);
    }

    std::chrono::milliseconds m_timeout;
    QElapsedTimer m_elapsed;
    QTimer m_deadline;
    QTimer m_progress;
};

hio::Status addSendCommand(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "send_command",
            .title = "Send a shell command",
            .description = sendCommandDescription,
            .inputSchema =
                Schema::object()
                    .property(QStringLiteral("device"), deviceArgument())
                    .property(QStringLiteral("command"),
                              Schema::string(QStringLiteral("One shell command line."))
                                  .minLength(1)
                                  .maxLength(4096),
                              Required::Yes)
                    .property(QStringLiteral("timeout"),
                              Schema::number(QStringLiteral("Seconds to wait at most (0.1-120)."))
                                  .minimum(0.1)
                                  .maximum(120)
                                  .defaultValue(5))
                    .json(),
            .outputSchema = objectOf(
                {{"command", typed("string")},
                 {"output",
                  arrayOf(objectOf({{"dir", typed("string")}, {"text", typed("string")}}))},
                 {"end", typed("string", "prompt, silence or timeout")},
                 {"interleaved", typed("boolean")},
                 {"log_cursor", typed("integer")},
                 {"log_cursor_end", typed("integer")},
                 {"terminal_cursor_end", typed("integer")},
                 {"truncated", typed("boolean")},
                 {"paused_by_operation", typed("boolean")}},
                {QStringLiteral("command"), QStringLiteral("output"), QStringLiteral("end")}),
            .annotations = control,
            .consent = {},
            .marker = true,
            .start = [&context](const hio::mcp::RequestContext& rc, const QJsonObject& args,
                                hio::mcp::ToolEnv& env) -> std::unique_ptr<hio::mcp::ToolCall> {
                auto device = resolveDevice(context.provider(), args);
                if (!device) {
                    return failed(std::move(device).error());
                }
                const auto& limits = env.limits().tools;
                const auto timeout =
                    std::max(limits.sendCommandMinTimeout,
                             secondsArgument(args, "timeout", limits.sendCommandMaxTimeout));
                return std::make_unique<SendCommandCall>(
                    context, rc, env, std::move(*device),
                    args.value(QStringLiteral("command")).toString(), timeout);
            },
            .validate = [](const QJsonObject& args, const hio::mcp::Limits& limits)
                -> std::optional<hio::mcp::ValidationError> {
                const QString command = args.value(QStringLiteral("command")).toString();
                if (command.contains(QLatin1Char('\n')) || command.contains(QLatin1Char('\r'))) {
                    return invalid(QStringLiteral("command"),
                                   QStringLiteral("must be a single line"));
                }
                if (std::cmp_greater(command.toUtf8().size(), limits.tools.commandMaxBytes)) {
                    return invalid(QStringLiteral("command"),
                                   QStringLiteral("is longer than %1 bytes")
                                       .arg(limits.tools.commandMaxBytes));
                }
                return std::nullopt;
            }});
}

hio::Status addReadTerminal(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry, hio::mcp::ToolSpec{
                      .name = "read_terminal",
                      .title = "Read the terminal",
                      .description = readTerminalDescription,
                      .inputSchema =
                          Schema::object()
                              .property(QStringLiteral("device"), deviceArgument())
                              .property(QStringLiteral("lines"),
                                        Schema::integer(QStringLiteral("Lines to return (1-5000)."))
                                            .minimum(1)
                                            .maximum(5000)
                                            .defaultValue(100))
                              .property(QStringLiteral("after_cursor"),
                                        Schema::integer(
                                            QStringLiteral("Return only lines after this cursor."))
                                            .minimum(0))
                              .json(),
                      .outputSchema = readOutputSchema(false),
                      .annotations = readOnly,
                      .consent = {},
                      .marker = false,
                      .start =
                          [&context](const hio::mcp::RequestContext&, const QJsonObject& args,
                                     hio::mcp::ToolEnv& env) {
                              auto device = resolveDevice(context.provider(), args);
                              if (!device) {
                                  return failed(std::move(device).error());
                              }
                              std::optional<std::uint64_t> cursor;
                              if (args.contains(QStringLiteral("after_cursor"))) {
                                  cursor = static_cast<std::uint64_t>(
                                      args.value(QStringLiteral("after_cursor")).toInteger());
                              }
                              const ReadOutcome outcome =
                                  readLines(device->ref.console->log(), Channel::Shell, cursor,
                                            args.value(QStringLiteral("lines")).toInteger(),
                                            std::nullopt, env.limits().tools);
                              return done(readResult(outcome, false, device->ref));
                          },
                      .validate = {}});
}

hio::Status addReadLog(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "read_log",
            .title = "Read the device log",
            .description = readLogDescription,
            .inputSchema =
                Schema::object()
                    .property(QStringLiteral("device"), deviceArgument())
                    .property(QStringLiteral("lines"),
                              Schema::integer(QStringLiteral("Lines to return; -1 = all buffered."))
                                  .minimum(-1)
                                  .maximum(5000)
                                  .defaultValue(-1))
                    .property(
                        QStringLiteral("after_cursor"),
                        Schema::integer(QStringLiteral("Return only lines after this cursor."))
                            .minimum(0))
                    .property(QStringLiteral("pattern"),
                              Schema::string(QStringLiteral("Case-insensitive regular expression."))
                                  .maxLength(512))
                    .json(),
            .outputSchema = readOutputSchema(true),
            .annotations = readOnly,
            .consent = {},
            .marker = false,
            .start =
                [&context](const hio::mcp::RequestContext&, const QJsonObject& args,
                           hio::mcp::ToolEnv& env) {
                    auto device = resolveDevice(context.provider(), args);
                    if (!device) {
                        return failed(std::move(device).error());
                    }
                    std::optional<std::uint64_t> cursor;
                    if (args.contains(QStringLiteral("after_cursor"))) {
                        cursor = static_cast<std::uint64_t>(
                            args.value(QStringLiteral("after_cursor")).toInteger());
                    }
                    std::optional<QRegularExpression> regex;
                    const QString pattern = args.value(QStringLiteral("pattern")).toString();
                    if (!pattern.isEmpty()) {
                        regex = compilePattern(pattern);
                    }
                    const ReadOutcome outcome = readLines(
                        device->ref.console->log(), Channel::Log, cursor,
                        args.value(QStringLiteral("lines")).toInteger(), regex, env.limits().tools);
                    return done(readResult(outcome, true, device->ref));
                },
            .validate =
                [](const QJsonObject& args, const hio::mcp::Limits& limits) {
                    return checkPattern(args, limits, false);
                }});
}

hio::Status addWaitForState(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "wait_for_state",
            .title = "Wait for output",
            .description = waitForStateDescription,
            .inputSchema =
                Schema::object()
                    .property(QStringLiteral("device"), deviceArgument())
                    .property(
                        QStringLiteral("pattern"),
                        Schema::string(QStringLiteral("Case-insensitive regular expression to "
                                                      "wait for."))
                            .minLength(1)
                            .maxLength(512),
                        Required::Yes)
                    .property(QStringLiteral("timeout"),
                              Schema::number(QStringLiteral("Seconds to wait (1-600)."))
                                  .minimum(1)
                                  .maximum(600)
                                  .defaultValue(60))
                    .property(
                        QStringLiteral("command"),
                        Schema::string(QStringLiteral("Shell command to send every "
                                                      "poll_interval seconds (polling mode)."))
                            .minLength(1)
                            .maxLength(4096))
                    .property(QStringLiteral("poll_interval"),
                              Schema::number(QStringLiteral("Seconds between commands (0.5-60)."))
                                  .minimum(0.5)
                                  .maximum(60)
                                  .defaultValue(5))
                    .property(QStringLiteral("source"),
                              Schema::string(QStringLiteral("Where to look."))
                                  .enumValues({QStringLiteral("log"), QStringLiteral("terminal"),
                                               QStringLiteral("both")})
                                  .defaultValue(QStringLiteral("log")))
                    .json(),
            .outputSchema = objectOf({{"status", typed("string", "ok or timeout")},
                                      {"matched", typed("boolean")},
                                      {"pattern", typed("string")},
                                      {"matched_line", typed("string")},
                                      {"elapsed", typed("number")},
                                      {"last_lines", arrayOf(typed("string"))},
                                      {"log_cursor_end", typed("integer")},
                                      {"terminal_cursor_end", typed("integer")},
                                      {"paused_by_operation", typed("boolean")}},
                                     {QStringLiteral("status"), QStringLiteral("matched")}),
            .annotations = readOnly,
            .consent = {},
            .marker = true,
            .start = [&context](const hio::mcp::RequestContext& rc, const QJsonObject& args,
                                hio::mcp::ToolEnv& env) -> std::unique_ptr<hio::mcp::ToolCall> {
                auto device = resolveDevice(context.provider(), args);
                if (!device) {
                    return failed(std::move(device).error());
                }
                auto regex = compilePattern(args.value(QStringLiteral("pattern")).toString());
                if (!regex) {
                    return failed(invalidArgument(QStringLiteral("Invalid pattern."), {}));
                }
                const QString source = args.value(QStringLiteral("source")).toString();
                ChannelMask mask = ChannelMask::Log;
                if (source == QStringLiteral("terminal")) {
                    mask = ChannelMask::Shell;
                } else if (source == QStringLiteral("both")) {
                    mask = ChannelMask::All;
                }
                const auto& limits = env.limits().tools;
                return std::make_unique<WaitForStateCall>(
                    context, rc, env, std::move(*device), std::move(*regex), mask,
                    secondsArgument(args, "timeout", limits.waitMaxTimeout),
                    args.value(QStringLiteral("command")).toString(),
                    secondsArgument(args, "poll_interval", limits.waitMaxTimeout));
            },
            .validate = [](const QJsonObject& args, const hio::mcp::Limits& limits)
                -> std::optional<hio::mcp::ValidationError> {
                if (auto bad = checkPattern(args, limits, true)) {
                    return bad;
                }
                const QString command = args.value(QStringLiteral("command")).toString();
                if (command.contains(QLatin1Char('\n')) || command.contains(QLatin1Char('\r'))) {
                    return invalid(QStringLiteral("command"),
                                   QStringLiteral("must be a single line"));
                }
                return std::nullopt;
            }});
}

hio::Status addWaitForConnection(hio::mcp::Registry& registry, ToolsContext& context) {
    return addTool(
        registry,
        hio::mcp::ToolSpec{
            .name = "wait_for_connection",
            .title = "Wait for the connection",
            .description = waitForConnectionDescription,
            .inputSchema = Schema::object()
                               .property(QStringLiteral("device"), deviceArgument())
                               .property(QStringLiteral("timeout"),
                                         Schema::number(QStringLiteral("Seconds to wait (1-600)."))
                                             .minimum(1)
                                             .maximum(600)
                                             .defaultValue(30))
                               .json(),
            .outputSchema = objectOf({{"status", typed("string", "ok or timeout")},
                                      {"connected", typed("boolean")},
                                      {"state", typed("string")},
                                      {"error", nullable("object")},
                                      {"elapsed", typed("number")}},
                                     {QStringLiteral("connected"), QStringLiteral("state")}),
            .annotations = readOnly,
            .consent = {},
            .marker = false,
            .start = [&context](const hio::mcp::RequestContext& rc, const QJsonObject& args,
                                hio::mcp::ToolEnv& env) -> std::unique_ptr<hio::mcp::ToolCall> {
                auto device = resolveDevice(context.provider(), args);
                if (!device) {
                    return failed(std::move(device).error());
                }
                return std::make_unique<WaitForConnectionCall>(
                    context, rc, env, std::move(*device),
                    secondsArgument(args, "timeout", env.limits().tools.waitMaxTimeout));
            },
            .validate = {}});
}

} // namespace

hio::Status registerConsoleTools(hio::mcp::Registry& registry, ToolsContext& context) {
    for (const auto& add : {&addSendCommand, &addReadTerminal, &addReadLog, &addWaitForState,
                            &addWaitForConnection}) {
        if (auto added = add(registry, context); !added) {
            return added;
        }
    }
    return {};
}

} // namespace hio::mcp::tools::detail
