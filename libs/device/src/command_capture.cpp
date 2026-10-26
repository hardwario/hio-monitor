#include "hio/device/command_capture.h"

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/console_session.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"

#include <QList>
#include <QObject>
#include <QTimer>
#include <QtCore/qnamespace.h>
#include <QtCore/qtmetamacros.h>

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace hio::device {

std::string_view captureEndId(CaptureEnd end) noexcept {
    switch (end) {
    case CaptureEnd::Prompt:
        return "prompt";
    case CaptureEnd::Silence:
        return "silence";
    case CaptureEnd::Timeout:
        return "timeout";
    }
    return "timeout";
}

std::string_view shellPromptOf(const ConsoleSession& session) {
    if (session.identity().transport == Transport::Ble) {
        const auto& ble = session.profile().ble;
        return ble ? ble->shellPrompt : std::string_view();
    }
    const TargetProfile* target = findTarget(session.profile(), session.consoleTargetId());
    return target != nullptr ? target->shellPrompt : std::string_view();
}

Result<std::unique_ptr<CommandCapture>>
CommandCapture::start(ConsoleSession& session, std::string command, CaptureOptions options) {
    if (options.silent && session.shellCaptureActive()) {
        return std::unexpected(makeError(ErrorCode::InternalBusy, "a shell query runs"));
    }
    // Observe mode: a line held by a running silent capture is sent when the hold ends; its time
    // starts then (review P8).
    const bool held = !options.silent && session.shellCaptureActive();
    auto capture = std::make_unique<CommandCapture>(Passkey{}, session, command, options);
    if (options.silent) {
        capture->m_origin = session.newCaptureOrigin();
        if (Status sent = session.sendCaptureLine(std::move(command), capture->m_origin); !sent) {
            capture->stop();
            return std::unexpected(std::move(sent).error());
        }
    } else if (Status sent = session.sendLine(std::move(command)); !sent) {
        capture->stop();
        return std::unexpected(std::move(sent).error());
    }
    if (held) {
        CommandCapture* raw = capture.get();
        connect(
            &session, &ConsoleSession::shellCaptureEnded, raw, [raw] { raw->startDeadline(); },
            Qt::SingleShotConnection);
    } else {
        capture->startDeadline();
    }
    return capture;
}

CommandCapture::CommandCapture(Passkey /*key*/, ConsoleSession& session, std::string command,
                               CaptureOptions options)
    : m_session(&session), m_command(std::move(command)), m_options(options),
      m_promptMode(!shellPromptOf(session).empty()), m_startSeq(session.log().lastSeq()) {
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(m_options.timeout);
    connect(&m_deadline, &QTimer::timeout, this, [this] { end(CaptureEnd::Timeout); });
    m_silence.setSingleShot(true);
    m_silence.setInterval(m_options.silence);
    connect(&m_silence, &QTimer::timeout, this, [this] { end(CaptureEnd::Silence); });
    connect(&session, &ConsoleSession::promptSeen, this, [this] {
        if (m_echoSeen && m_promptMode) {
            end(CaptureEnd::Prompt);
        }
    });
    connect(&session, &QObject::destroyed, this, [this] { stop(); });
    if (m_options.silent) {
        // Review P3: a query does not outlive the attach it was sent on.
        connect(&session, &ConsoleSession::connectionChanged, this,
                [this](const ConnectionEvent& event) {
                    if (event.state != ConnectionState::Connected && !m_stopped) {
                        stop();
                        emit aborted();
                    }
                });
        session.setShellCapture([this](const ConsoleLine& line) { return offer(line); });
        m_captureInstalled = true;
    } else {
        connect(&session, &ConsoleSession::linesReceived, this,
                [this](const QList<ConsoleLine>& lines) {
                    for (const ConsoleLine& line : lines) {
                        if (m_stopped) {
                            return;
                        }
                        if (line.seq > m_startSeq && line.channel == Channel::Shell) {
                            (void)offer(line);
                        }
                    }
                });
    }
}

CommandCapture::~CommandCapture() { stop(); }

void CommandCapture::startDeadline() {
    if (!m_stopped) {
        m_deadline.start();
    }
}

bool CommandCapture::offer(const ConsoleLine& line) {
    if (m_stopped) {
        return false;
    }
    if (!m_echoSeen) {
        // Output before the echo belongs to an earlier command (ADR-0104). Silent mode knows its
        // echo by origin (review P1); observe mode by its text.
        m_echoSeen = line.dir == Direction::In &&
                     (m_options.silent ? line.origin == m_origin : line.text == m_command);
        return m_echoSeen;
    }
    if (line.dir == Direction::Marker) {
        return false;
    }
    if (line.dir == Direction::In) {
        if (m_options.silent) {
            return false; // another line's echo stays in the log (review P1)
        }
        m_collected.interleaved = true; // ADR-0105: another command was sent meanwhile
        append(line);
        return true;
    }
    // A shell that echoes the typed line itself: drop that copy (first line only).
    if (!m_outputStarted && line.text == m_command) {
        m_outputStarted = true;
        return true;
    }
    m_outputStarted = true;
    append(line);
    if (!m_promptMode) {
        m_silence.start();
    }
    return true;
}

void CommandCapture::append(const ConsoleLine& line) {
    if (m_collected.lines.size() >= m_options.maxLines ||
        m_bytes + line.text.size() > m_options.maxBytes) {
        m_collected.truncated = true;
        return;
    }
    m_bytes += line.text.size();
    m_collected.lines.push_back(line);
}

void CommandCapture::end(CaptureEnd reason) {
    if (m_stopped || isFinished()) {
        return;
    }
    stop();
    m_collected.end = reason;
    m_result = std::move(m_collected);
    emit finished();
}

void CommandCapture::cancel() { stop(); }

void CommandCapture::stop() {
    if (m_stopped) {
        return;
    }
    m_stopped = true;
    m_deadline.stop();
    m_silence.stop();
    if (m_session) {
        disconnect(m_session, nullptr, this, nullptr);
        if (m_captureInstalled) {
            m_captureInstalled = false;
            m_session->setShellCapture({}); // sends the held user lines
        }
    }
}

} // namespace hio::device
