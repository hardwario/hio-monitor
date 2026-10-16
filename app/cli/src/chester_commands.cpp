#include "chester_commands.h"

#include "cli_app.h"
#include "cli_log.h"
#include "command_response.h"
#include "console_events.h"
#include "console_ui.h"
#include "flash_commands.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/command_history.h"
#include "hio/device/console_line.h"
#include "hio/device/device_session.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/device/session_registry.h"
#include "hio/hw/jlink/rtt_channel.h"
#include "jlink_commands.h"
#include "output.h"
#include "probe_options.h"
#include "terminal.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QTimer>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::cli {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view profileId = "chester";
constexpr std::string_view targetId = "app";
// `chester app command`: margin on top of lines x RTT write timeout for the echoes (review R23).
constexpr std::chrono::milliseconds writeBudgetMargin{500};

const char* const powerCycleTip =
    "TIP: After a J-Link connection, power-cycle the device; otherwise the CPU stays in debug "
    "mode with increased power consumption.";

struct ConsoleOptions {
    ProbeOptions probe;
    bool reset = false;
    int latencyMs = 0;
    std::string historyFile;
    std::string consoleFile;
    bool consoleFileGiven = false;
    std::string coredumpFile;
    bool autoReconnect = false;
    double lingerSeconds = 1.0;
};

struct CommandOptions {
    ProbeOptions probe;
    bool reset = false;
    double timeoutSeconds = 1.0;
    std::string consoleFile;
    bool consoleFileGiven = false;
    std::string command;
};

const device::TargetProfile& chesterApp() {
    const device::DeviceProfile* profile = device::ProfileRegistry::find(profileId);
    return *device::findTarget(*profile, targetId);
}

Result<device::SessionOptions> sessionOptions(const ProbeOptions& group, const ProbeOptions& own,
                                              int latencyMs, bool autoReconnect, bool reset,
                                              const std::string& consoleFile,
                                              bool consoleFileGiven) {
    device::SessionOptions options;
    options.profile = device::ProfileRegistry::find(profileId);
    options.targetId = targetId;
    options.probeSn = serialNumberOf(group, own);
    auto speed = speedOf(group, own, chesterApp().defaultSpeedKhz);
    if (!speed) {
        return std::unexpected(std::move(speed).error());
    }
    options.speedKhz = *speed;
    options.latencyMs = latencyMs;
    options.autoReconnect = autoReconnect;
    options.resetOnOpen = reset;
    if (consoleFileGiven) {
        // --console-file '' disables the console file.
        options.consoleFile = fs::path(toQString(consoleFile).toStdU16String());
    }
    return options;
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

// ADR-0110: before connecting at a fixed speed below 125 kHz, where the console may not be found
// within the (capped) attach deadline. Logged; on stderr in human mode (like the other warnings
// outside a console event stream).
void warnAboutSlowSpeed(const Context& context, std::optional<int> speedKhz) {
    const std::optional<std::string> warning = device::slowSpeedWarning(speedKhz);
    if (!warning) {
        return;
    }
    qCWarning(lcCli).noquote() << toQString(*warning);
    if (context.mode == OutputMode::Human) {
        std::cerr << "Warning: " << *warning << '\n';
    }
}

// ---- console --------------------------------------------------------------------------------

Result<CommandResult> console(const Context& context, const GlobalOptions& globals,
                              const ProbeOptions& group, const ConsoleOptions& options) {
    if (!options.coredumpFile.empty()) {
        qCWarning(lcCli) << "--coredump-file is accepted for compatibility and has no effect";
    }
    auto location = locateJLinkLibrary(globals);
    if (!location) {
        return std::unexpected(std::move(location).error());
    }
    device::SessionRegistry registry;
    auto sessionOpts =
        sessionOptions(group, options.probe, options.latencyMs, options.autoReconnect,
                       options.reset, options.consoleFile, options.consoleFileGiven);
    if (!sessionOpts) {
        return std::unexpected(std::move(sessionOpts).error());
    }
    const bool announceProbe = !sessionOpts->probeSn;
    const std::optional<int> speed = sessionOpts->speedKhz;
    warnAboutSlowSpeed(context, speed);
    auto session = device::DeviceSession::open(registry, *location, std::move(*sessionOpts));
    if (!session) {
        return std::unexpected(std::move(session).error());
    }
    const bool interactive =
        context.mode == OutputMode::Human && stdinIsTerminal() && stdoutIsTerminal();
    // The console's own stop request: Ctrl+C / SIGTERM (context.stop) or a closed stdout (review
    // R32: `hio ... console --json | head` ends like Ctrl+C, the session closes normally).
    std::stop_source consoleStop;
    const std::stop_callback forwardStop(context.stop, [&consoleStop] {
        consoleStop.request_stop(); // may run on the signal watcher thread (thread-safe)
    });
    Context consoleContext = context;
    consoleContext.stop = consoleStop.get_token();
    ConsoleView view(consoleContext, **session, interactive && stdoutColors(), announceProbe);
    view.setOutputClosedHandler([&consoleStop] { consoleStop.request_stop(); });
    // Adds the reason to a stop caused by the closed stdout.
    const auto withReason = [&view](Error error) {
        if (error.code == ErrorCode::Cancelled && view.outputClosed()) {
            // Detected on the next write after the reader went away (review R32, R44).
            return outputClosedError("the standard output was closed");
        }
        return error;
    };
    view.printStatus(std::format("Connecting to {} at {}...", chesterApp().jlinkDevice,
                                 jlink::speedDetail(speed)));
    if (const Status connected =
            waitForConsoleConnection(consoleContext, **session, view, options.autoReconnect,
                                     device::connectTimeoutForSpeed(speed));
        !connected) {
        return std::unexpected(withReason(connected.error()));
    }

    ConsoleUiOptions ui;
    ui.interactive = interactive;
    ui.linger =
        std::chrono::milliseconds(static_cast<std::int64_t>(options.lingerSeconds * 1000.0));
    if (interactive) {
        ui.historyFile = options.historyFile.empty()
                             ? device::CommandHistory::defaultFile(profileId, targetId)
                             : fs::path(toQString(options.historyFile).toStdU16String());
    }
    const Status status = runConsole(consoleContext, **session, view, ui);
    (*session)->close(); // writes the console file's trailer and flushes it
    view.printStopped();
    if (!status) {
        return std::unexpected(withReason(status.error()));
    }
    return CommandResult{.data = QJsonValue(), .humanText = powerCycleTip, .streamed = true};
}

// ---- command --------------------------------------------------------------------------------

// Collects the response of `chester app command` from the moment the command is sent.
class ResponseCollector {
public:
    ResponseCollector(const Context& context, device::DeviceSession& session) {
        QObject::connect(&session, &device::DeviceSession::linesReceived, &m_receiver,
                         [this](const QList<device::ConsoleLine>& lines) { collect(lines); });
        QObject::connect(&session, &device::DeviceSession::connectionChanged, &m_receiver,
                         [this](const device::ConnectionEvent& event) {
                             if (m_collecting && event.error &&
                                 event.state != device::ConnectionState::Connected) {
                                 m_failure = event.error;
                             }
                             if (m_collecting &&
                                 event.state != device::ConnectionState::Connected) {
                                 m_disconnected = true; // no further line will be written
                             }
                         });
        QObject::connect(&session, &device::DeviceSession::warning, &m_receiver,
                         [this, &context](const Error& error) {
                             qCWarning(lcCli).noquote() << toQString(error.message);
                             if (m_collecting && endsTheCommand(error.code)) {
                                 m_failure = error; // the command was not written (review R16)
                             } else if (context.mode == OutputMode::Human) {
                                 std::cerr << humanWarning(error) << '\n';
                             }
                         });
    }

    void start() { m_collecting = true; }
    // Sent lines echoed by the session, i.e. written to the device.
    [[nodiscard]] std::size_t written() const { return m_written; }
    // The collected output and log lines, bounded (review R42).
    [[nodiscard]] const CommandResponse& response() const { return m_response; }
    // A connection error while collecting (e.g. the write timed out).
    [[nodiscard]] const std::optional<Error>& failure() const { return m_failure; }
    // The session left Connected while collecting (with or without an error).
    [[nodiscard]] bool disconnected() const { return m_disconnected; }

private:
    void collect(const QList<device::ConsoleLine>& lines) {
        for (const device::ConsoleLine& line : lines) {
            if (m_collecting && line.dir == device::Direction::In) {
                ++m_written;
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

    // Warnings that mean a command line did not reach the device.
    static bool endsTheCommand(ErrorCode code) {
        return code == ErrorCode::TargetNotConnected ||
               code == ErrorCode::TargetRttBufferNotFound ||
               code == ErrorCode::TargetRttWriteTimeout || code == ErrorCode::TargetRttWriteFailed;
    }

    bool m_collecting = false;
    bool m_disconnected = false;
    std::size_t m_written = 0;
    CommandResponse m_response;
    std::optional<Error> m_failure;
    QObject m_receiver;
};

// Runs the event loop until `done` returns true or `duration` passed; cancelled on Ctrl+C /
// SIGTERM.
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

// Queues the command lines in order; a full command queue keeps the rest for the next send()
// (review R39, as the console's LineSender).
class LineQueue {
public:
    LineQueue(device::DeviceSession& session, const std::vector<std::string>& lines)
        : m_session(&session), m_lines(&lines) {}

    void send() {
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

    // A send error other than a full queue (e.g. the session was closed).
    [[nodiscard]] const std::optional<Error>& error() const noexcept { return m_error; }

private:
    device::DeviceSession* m_session;        // non-owning; outlives the queue
    const std::vector<std::string>* m_lines; // non-owning; outlives the queue
    std::size_t m_next = 0;
    std::optional<Error> m_error;
};

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

Result<CommandResult> command(const Context& context, const GlobalOptions& globals,
                              const ProbeOptions& group, const CommandOptions& options) {
    auto location = locateJLinkLibrary(globals);
    if (!location) {
        return std::unexpected(std::move(location).error());
    }
    device::SessionRegistry registry;
    auto sessionOpts = sessionOptions(group, options.probe, chesterApp().defaultLatencyMs, false,
                                      options.reset, options.consoleFile, options.consoleFileGiven);
    if (!sessionOpts) {
        return std::unexpected(std::move(sessionOpts).error());
    }
    const bool announceProbe = !sessionOpts->probeSn;
    const std::optional<int> speed = sessionOpts->speedKhz;
    warnAboutSlowSpeed(context, speed);
    auto session = device::DeviceSession::open(registry, *location, std::move(*sessionOpts));
    if (!session) {
        return std::unexpected(std::move(session).error());
    }
    device::DeviceSession& device = **session;
    ResponseCollector collector(context, device);
    if (const Status connected =
            device::waitUntilConnected(device, device::connectTimeoutForSpeed(speed), context.stop);
        !connected) {
        device.close();
        return std::unexpected(connected.error());
    }
    if (announceProbe) {
        qCInfo(lcCli) << "Using J-Link" << device.probe().serialNumber;
        if (context.mode == OutputMode::Human) {
            std::cerr << std::format("Using J-Link {}\n", device.probe().serialNumber);
        }
    }

    // py-hardwario: send every line, then collect for the timeout counted from when the last
    // line was written (its echo arrived). The lines are queued while the event loop runs, a full
    // command queue (more than 256 lines) keeps the rest for the next round (review R39, as the
    // console's LineSender). The worker writes them one after another, each within the RTT write
    // timeout, so the echoes are awaited for one write timeout per line, counted as the lines are
    // written (line n+1 until start + (n + 1) x write timeout + 500 ms, n = lines written so
    // far; at most lines x write timeout + 500 ms in total), or until the worker reports a failed
    // write or the session leaves Connected; the command takes at most that + timeout (review
    // fix 7, R23).
    collector.start();
    const std::vector<std::string> lines = splitLines(options.command);
    const auto writeTimeout = jlink::RttTiming{}.writeTimeout;
    const auto writeBudget =
        (writeTimeout * static_cast<std::int64_t>(lines.size())) + writeBudgetMargin;
    const auto started = std::chrono::steady_clock::now();
    LineQueue queue(device, lines);
    queue.send();
    Status collected = runEventsUntil(
        [&] {
            queue.send();
            const auto lineBudget =
                (writeTimeout * static_cast<std::int64_t>(collector.written() + 1)) +
                writeBudgetMargin;
            return queue.error().has_value() || collector.written() >= lines.size() ||
                   collector.failure().has_value() || collector.disconnected() ||
                   std::chrono::steady_clock::now() - started >= lineBudget;
        },
        writeBudget, context.stop);
    if (queue.error()) {
        device.close();
        return std::unexpected(*queue.error());
    }
    if (collected && !collector.failure() && collector.written() < lines.size()) {
        // Not every line was written (review R16): the session disconnected, or the worker
        // could not complete the writes within the budget.
        device.close();
        const std::string detail =
            std::format("{} of {} command lines written", collector.written(), lines.size());
        return std::unexpected(collector.disconnected()
                                   ? makeError(ErrorCode::TargetNotConnected, detail)
                                   : makeError(ErrorCode::TargetRttWriteTimeout, detail));
    }
    if (collected && !collector.failure()) {
        // The collection ends early when the response bound is reached (review R42).
        collected = runEventsUntil(
            [&collector] { return collector.response().truncated(); },
            std::chrono::milliseconds(static_cast<std::int64_t>(options.timeoutSeconds * 1000.0)),
            context.stop);
    }
    device.close();
    if (!collected) {
        return std::unexpected(collected.error());
    }
    if (collector.failure()) {
        return std::unexpected(*collector.failure());
    }
    return commandResult(context, options.command, collector.response());
}

} // namespace

Result<std::optional<int>> parseJLinkSpeed(std::string_view text) {
    if (text == "auto") {
        return std::optional<int>();
    }
    // Decimal digits only (no sign, spaces or suffix); at most 5 digits, so no overflow.
    constexpr std::size_t maxDigits = 5;
    const bool digits = !text.empty() && text.size() <= maxDigits &&
                        std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
    int kiloHertz = 0;
    for (const char c : digits ? text : std::string_view()) {
        kiloHertz = (kiloHertz * 10) + (c - '0');
    }
    if (!digits || kiloHertz < device::minSpeedKhz || kiloHertz > device::maxSpeedKhz) {
        Error invalid = makeError(ErrorCode::UsageInvalidArgument, std::string(text));
        invalid.message = std::format("The J-Link speed must be auto or between {} and {} kHz.",
                                      device::minSpeedKhz, device::maxSpeedKhz);
        return std::unexpected(std::move(invalid));
    }
    return std::optional<int>(kiloHertz);
}

void registerChesterCommands(CLI::App& app, CommandSlot& slot, const GlobalOptions& globals) {
    CLI::App* chester = app.add_subcommand("chester", "HARDWARIO CHESTER");
    chester->require_subcommand(1);
    CLI::App* appGroup = chester->add_subcommand("app", "Application SoC commands");
    appGroup->require_subcommand(1);
    auto group = std::make_shared<ProbeOptions>();
    addProbeOptions(*appGroup, *group);

    // console
    auto consoleOptions = std::make_shared<ConsoleOptions>();
    consoleOptions->latencyMs = chesterApp().defaultLatencyMs;
    CLI::App* consoleCommand = appGroup->add_subcommand(
        "console", "Start the interactive console for the shell and the device log");
    consoleCommand->add_flag("--reset", consoleOptions->reset,
                             "Reset the application firmware before attaching");
    consoleCommand
        ->add_option("--latency", consoleOptions->latencyMs, "Latency for RTT readout in ms")
        ->capture_default_str()
        ->option_text("MS");
    consoleCommand
        ->add_option("--history-file", consoleOptions->historyFile,
                     "Command history file (default: in the application data)")
        ->option_text("PATH");
    CLI::Option* consoleFile = consoleCommand
                                   ->add_option("--console-file", consoleOptions->consoleFile,
                                                "Console log file ('' = none; default: one file "
                                                "per session in the application data)")
                                   ->option_text("PATH");
    consoleCommand
        ->add_option("--coredump-file", consoleOptions->coredumpFile,
                     "Accepted for py-hardwario compatibility; no effect")
        ->option_text("PATH");
    consoleCommand
        ->add_option("--linger", consoleOptions->lingerSeconds,
                     "Non-interactive and --json mode: seconds to keep collecting output after the "
                     "end of input (ignored in interactive mode)")
        ->capture_default_str()
        ->check(CLI::Range(0.0, 60.0))
        ->option_text("S");
    consoleCommand->add_flag("--auto-reconnect", consoleOptions->autoReconnect,
                             "Keep re-attaching RTT after the link drops, instead of leaving "
                             "the console disconnected until F4");
    addProbeOptions(*consoleCommand, consoleOptions->probe);
    consoleCommand->callback([&slot, &globals, group, consoleOptions, consoleFile] {
        consoleOptions->consoleFileGiven = consoleFile->count() > 0;
        slot.command = [&globals, group, consoleOptions](const Context& context) {
            return console(context, globals, *group, *consoleOptions);
        };
    });

    // command
    auto commandOptions = std::make_shared<CommandOptions>();
    CLI::App* commandCommand =
        appGroup->add_subcommand("command", "Send a command to the device and print the response");
    commandCommand->add_flag("--reset", commandOptions->reset,
                             "Reset the application firmware before attaching");
    commandCommand
        ->add_option("-t,--timeout", commandOptions->timeoutSeconds,
                     "Seconds to collect the response after sending")
        ->capture_default_str()
        ->check(CLI::Range(0.001, 3600.0))
        ->option_text("TIMEOUT");
    CLI::Option* commandConsoleFile =
        commandCommand
            ->add_option("--console-file", commandOptions->consoleFile,
                         "Console log file ('' = none; default: one file per run in the "
                         "application data)")
            ->option_text("PATH");
    addProbeOptions(*commandCommand, commandOptions->probe);
    commandCommand->add_option("command", commandOptions->command, "Shell command(s), one per line")
        ->required();
    commandCommand->callback([&slot, &globals, group, commandOptions, commandConsoleFile] {
        commandOptions->consoleFileGiven = commandConsoleFile->count() > 0;
        slot.command = [&globals, group, commandOptions](const Context& context) {
            return command(context, globals, *group, *commandOptions);
        };
    });

    // M2: flash, erase, reset, recover, pib read, uicr read; the `chester lte` group.
    registerAppFlashCommands(*appGroup, group, slot, globals);
    registerLteCommands(*chester, slot, globals);
}

} // namespace hio::cli
