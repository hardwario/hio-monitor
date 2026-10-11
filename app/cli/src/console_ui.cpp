#include "console_ui.h"

#include "cli_app.h"
#include "cli_log.h"
#include "console_events.h"
#include "hio/core/bounded_queue.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/command_history.h"
#include "hio/device/console_line.h"
#include "hio/device/device_session.h"
#include "hio/device/session_events.h"
#include "output.h"
#include "terminal.h"

#include <QEventLoop>
#include <QJsonValue>
#include <QList>
#include <QLoggingCategory>
#include <QObject>
#include <QTimer>
#include <QtTypes>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <iostream>
#include <optional>
#include <replxx.hxx>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace hio::cli {

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto tickInterval = std::chrono::milliseconds(20);
// After the end of input: wait until the sent commands were written (at most this long) ...
constexpr auto eofWriteWait = std::chrono::seconds(3);

} // namespace

// ---- ConsoleView ---------------------------------------------------------------------------

ConsoleView::ConsoleView(const Context& context, device::DeviceSession& session, bool colors,
                         bool announceProbe)
    : m_context(&context), m_session(&session), m_colors(colors), m_announceProbe(announceProbe) {
    QObject::connect(&session, &device::DeviceSession::linesReceived, &m_connections,
                     [this](const QList<device::ConsoleLine>& lines) { onLines(lines); });
    QObject::connect(&session, &device::DeviceSession::connectionChanged, &m_connections,
                     [this](const device::ConnectionEvent& event) { onConnection(event); });
    QObject::connect(&session, &device::DeviceSession::warning, &m_connections,
                     [this](const Error& error) { onWarning(error); });
    QObject::connect(&session, &device::DeviceSession::linesDropped, &m_connections,
                     [this](quint64 count) { onDropped(count); });
}

ConsoleView::~ConsoleView() = default;

void ConsoleView::setPrinter(Printer printer, bool showEcho) {
    m_printer = std::move(printer);
    m_showEcho = showEcho;
}

void ConsoleView::setOutputClosedHandler(std::function<void()> handler) {
    m_onOutputClosed = std::move(handler);
}

void ConsoleView::print(ConsoleStream stream, std::string_view text) {
    if (m_printer) {
        m_printer(stream, text);
        return;
    }
    std::ostream& out = stream == ConsoleStream::Out ? std::cout : std::cerr;
    out << text << '\n' << std::flush;
    checkOutput();
}

void ConsoleView::printEvent(const QJsonValue& event) {
    m_context->output->printEvent(event, {});
    checkOutput();
}

// A failed write to stdout (EPIPE: SIGPIPE is ignored) becomes a stop request, so the session
// closes normally and the console file is flushed (review R32).
void ConsoleView::checkOutput() {
    if (m_outputClosed || m_context->output == nullptr || !m_context->output->outFailed()) {
        return;
    }
    m_outputClosed = true;
    qCWarning(lcCli) << "Writing to the standard output failed (closed pipe); stopping";
    if (m_onOutputClosed) {
        m_onOutputClosed();
    }
}

void ConsoleView::printStatus(std::string_view text) {
    if (m_context->mode == OutputMode::Human) {
        print(ConsoleStream::Err, text);
    }
}

void ConsoleView::printStopped() {
    // Exactly one terminal "stopped" event: not again when the session reported Stopped as its
    // last state, with or without an error (review R28).
    if (m_context->mode == OutputMode::Json && !m_stoppedPrinted) {
        m_stoppedPrinted = true;
        printEvent(connectionEventJson(device::ConnectionEvent{
            .state = device::ConnectionState::Stopped, .error = std::nullopt}));
    }
}

void ConsoleView::announceProbe() {
    if (!m_announceProbe || m_session->probe().serialNumber == 0) {
        return;
    }
    m_announceProbe = false;
    const auto probe = m_session->probe();
    qCInfo(lcCli) << "Using J-Link" << probe.serialNumber;
    printStatus(
        std::format("Using J-Link {} ({}{})", probe.serialNumber,
                    probe.product.empty() ? std::string("J-Link") : probe.product,
                    probe.firmware.empty() ? std::string() : ", firmware " + probe.firmware));
}

void ConsoleView::onLines(const QList<device::ConsoleLine>& lines) {
    for (const device::ConsoleLine& line : lines) {
        if (line.dir == device::Direction::In) {
            ++m_echoed;
        }
        if (m_context->mode == OutputMode::Json) {
            printEvent(lineEventJson(line));
            continue;
        }
        if (line.dir == device::Direction::In) {
            if (m_showEcho) {
                print(ConsoleStream::Out, "> " + line.text);
            }
            continue;
        }
        print(ConsoleStream::Out, humanLine(line, m_colors));
    }
}

void ConsoleView::onConnection(const device::ConnectionEvent& event) {
    announceProbe();
    if (event.state == device::ConnectionState::Stopped && event.error) {
        m_fatal = event.error;
    }
    if (m_context->mode == OutputMode::Json) {
        m_stoppedPrinted = event.state == device::ConnectionState::Stopped;
        printEvent(connectionEventJson(event));
        return;
    }
    if (event.state != device::ConnectionState::Connecting) {
        print(ConsoleStream::Err, humanConnection(event));
    }
}

void ConsoleView::onWarning(const Error& error) {
    if (m_context->mode == OutputMode::Json) {
        printEvent(warningEventJson(error));
        return;
    }
    print(ConsoleStream::Err, humanWarning(error));
}

void ConsoleView::onDropped(quint64 count) {
    if (m_context->mode == OutputMode::Json) {
        printEvent(droppedEventJson(count));
        return;
    }
    print(ConsoleStream::Err, std::format("-- {} lines dropped --", count));
}

// ---- runConsole -----------------------------------------------------------------------------

namespace {

// Runs a QEventLoop with a periodic tick until finish() is called.
class TickLoop {
public:
    [[nodiscard]] Status run(const std::function<void()>& tick) {
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, &m_loop, tick);
        timer.start(tickInterval);
        m_loop.exec();
        return m_result;
    }

    void finish(Status result = {}) {
        m_result = std::move(result);
        m_loop.quit();
    }

private:
    QEventLoop m_loop;
    Status m_result;
};

// Ctrl+C / SIGTERM (cancelled) or a fatal session error end every console.
bool finishedByStopOrError(const Context& context, const ConsoleView& view, TickLoop& loop) {
    if (context.stop.stop_requested()) {
        loop.finish(fail(ErrorCode::Cancelled));
        return true;
    }
    if (view.fatalError()) {
        loop.finish(std::unexpected(*view.fatalError()));
        return true;
    }
    return false;
}

// Sends lines to the session in order; a full command queue keeps the line for the next tick.
class LineSender {
public:
    explicit LineSender(device::DeviceSession& session) : m_session(&session) {}

    // Queues `line` after the pending one. False while a line is still pending.
    [[nodiscard]] bool hasPending() const noexcept { return m_pending.has_value(); }
    void add(std::string line) { m_pending = std::move(line); }

    // Sends the pending line; false when it has to wait (queue full).
    [[nodiscard]] bool flush() {
        if (!m_pending) {
            return true;
        }
        const Status status = m_session->sendLine(*m_pending);
        if (!status && status.error().code == ErrorCode::InternalQueueFull) {
            return false;
        }
        if (!status) {
            qCWarning(lcCli).noquote() << toQString(status.error().message);
        }
        m_pending.reset();
        ++m_sent;
        return true;
    }

    [[nodiscard]] std::uint64_t sent() const noexcept { return m_sent; }

private:
    device::DeviceSession* m_session; // non-owning; outlives the sender
    std::optional<std::string> m_pending;
    std::uint64_t m_sent = 0;
};

// Plain mode: stdin lines (or NDJSON with --json), end of input ends the console.
class PlainConsole {
public:
    PlainConsole(const Context& context, device::DeviceSession& session, const ConsoleView& view,
                 std::chrono::milliseconds linger)
        : m_context(&context), m_view(&view), m_sender(session), m_linger(linger) {}

    [[nodiscard]] Status run() {
        return m_loop.run([this] {
            if (!finishedByStopOrError(*m_context, *m_view, m_loop)) {
                pumpInput();
                checkEndOfInput();
            }
        });
    }

private:
    void pumpInput() {
        while (!m_endOfInput && m_sender.flush()) {
            auto next = m_reader.poll();
            if (!next) {
                return;
            }
            if (!*next) {
                m_endOfInput = Clock::now();
                qCDebug(lcCli) << "Console: end of input";
                return;
            }
            m_sender.add(std::move(**next));
        }
    }

    void checkEndOfInput() {
        if (!m_endOfInput || !m_sender.flush()) {
            return;
        }
        const auto now = Clock::now();
        if (!m_written &&
            (m_view->echoed() >= m_sender.sent() || now - *m_endOfInput >= eofWriteWait)) {
            m_written = now;
        }
        if (m_written && now - *m_written >= m_linger) {
            m_loop.finish();
        }
    }

    const Context* m_context;
    const ConsoleView* m_view;
    StdinLineReader m_reader;
    LineSender m_sender;
    TickLoop m_loop;
    std::chrono::milliseconds m_linger; // output collected after the end of input
    std::optional<Clock::time_point> m_endOfInput;
    std::optional<Clock::time_point> m_written;
};

// Interactive mode: replxx on an owned input thread, everything else on the main thread.
class InteractiveConsole {
public:
    InteractiveConsole(const Context& context, device::DeviceSession& session, ConsoleView& view,
                       const ConsoleUiOptions& options)
        : m_context(&context), m_session(&session), m_view(&view), m_options(&options),
          m_sender(session) {}

    [[nodiscard]] Status run() {
        openHistory();
        configureReplxx();
        m_view->setPrinter(
            [this](ConsoleStream /*stream*/, std::string_view text) {
                std::string line(text);
                line += '\n';
                m_rx.write(line.data(), static_cast<int>(line.size()));
            },
            false);
        // replxx's blocking input() runs here; it ends with nullptr (Ctrl+D, BAIL).
        std::jthread input([this](const std::stop_token& stop) { readInput(stop); });
        const Status result = m_loop.run([this] { tick(); });
        // End input() if it is still running (Ctrl+C signal, SIGTERM, fatal error) and join it.
        input.request_stop();
        m_entered.close();
        if (!m_quit.load()) {
            m_rx.emulate_key_press(Key::control('Q'));
        }
        input.join();
        m_view->setPrinter({}, true);
        return result;
    }

private:
    using Key = replxx::Replxx::KEY;
    using Action = replxx::Replxx::ACTION_RESULT;

    void openHistory() {
        if (!m_options->historyFile) {
            return;
        }
        auto opened = device::CommandHistory::open(*m_options->historyFile);
        if (!opened) {
            m_view->printStatus(humanWarning(opened.error()));
            return;
        }
        m_history = std::move(*opened);
        for (const std::string& entry : m_history->entries()) {
            m_rx.history_add(entry);
        }
    }

    void configureReplxx() {
        m_rx.install_window_change_handler();
        m_rx.set_max_history_size(static_cast<int>(device::CommandHistory::maxEntries));
        m_rx.set_unique_history(false);
        // F4: reconnect (handled on the main thread).
        m_rx.bind_key(Key::F4, [this](char32_t /*code*/) {
            m_reconnect.store(true);
            return Action::CONTINUE;
        });
        // Ctrl+Q: quit. Also emulated by the main thread to end input().
        m_rx.bind_key(Key::control('Q'), [](char32_t /*code*/) { return Action::BAIL; });
        // Ctrl+C: quit on an empty line, otherwise clear the line.
        m_rx.bind_key(Key::control('C'), [this](char32_t /*code*/) {
            const replxx::Replxx::State state = m_rx.get_state();
            if (state.text() == nullptr || std::string_view(state.text()).empty()) {
                return Action::BAIL;
            }
            m_rx.set_state(replxx::Replxx::State(""));
            return Action::CONTINUE;
        });
    }

    // Input thread. An exception is the end of input (review R27): never escapes the thread.
    void readInput(const std::stop_token& stop) {
        try {
            readLines(stop);
        } catch (...) {
            logThreadException("console input");
            m_quit.store(true);
        }
    }

    void readLines(const std::stop_token& stop) {
        while (!stop.stop_requested()) {
            const char* text = m_rx.input(m_options->prompt);
            if (text == nullptr) {
                m_quit.store(true);
                return;
            }
            std::string line(text);
            if (!line.empty()) {
                m_rx.history_add(line);
            }
            if (!m_entered.push(std::move(line), stop)) {
                return;
            }
        }
    }

    // Main thread.
    void tick() {
        if (finishedByStopOrError(*m_context, *m_view, m_loop)) {
            return;
        }
        if (m_quit.load()) {
            m_loop.finish();
            return;
        }
        if (m_reconnect.exchange(false)) {
            m_view->printStatus("-- reconnecting --");
            if (const Status status = m_session->reconnect(); !status) {
                m_view->printStatus(humanWarning(status.error()));
            }
        }
        while (m_sender.flush()) {
            auto line = m_entered.pop({}, std::chrono::milliseconds(0));
            if (!line) {
                return;
            }
            if (m_history) {
                m_history->add(*line);
            }
            m_sender.add(std::move(*line));
        }
    }

    const Context* m_context;
    device::DeviceSession* m_session;
    ConsoleView* m_view;
    const ConsoleUiOptions* m_options;
    replxx::Replxx m_rx;
    std::optional<device::CommandHistory> m_history;
    // Shared with the input thread: the entered lines (bounded) and two flags.
    BoundedQueue<std::string> m_entered{64, OverflowPolicy::Block};
    std::atomic<bool> m_quit{false};
    std::atomic<bool> m_reconnect{false};
    LineSender m_sender;
    TickLoop m_loop;
};

} // namespace

Status waitForConsoleConnection(const Context& context, device::DeviceSession& session,
                                ConsoleView& view, bool autoReconnect,
                                std::chrono::milliseconds timeout) {
    // With --auto-reconnect a first attempt that ends Disconnected does not end the console: it
    // shows the state and keeps retrying (rttt; decided 2026-10-09, amends ADR-0053's "the CLI
    // waits with waitUntilConnected" for this case).
    Status connected = autoReconnect ? device::waitForFirstAttempt(session, timeout, context.stop)
                                     : device::waitUntilConnected(session, timeout, context.stop);
    if (!connected) {
        session.close();
        view.printStopped(); // also after timeout / cancelled (review R34)
    }
    return connected;
}

Status runConsole(const Context& context, device::DeviceSession& session, ConsoleView& view,
                  const ConsoleUiOptions& options) {
    if (options.interactive && context.mode == OutputMode::Human) {
        InteractiveConsole console(context, session, view, options);
        return console.run();
    }
    PlainConsole console(context, session, view, options.linger);
    return console.run();
}

} // namespace hio::cli
