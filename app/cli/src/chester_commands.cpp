#include "chester_commands.h"

#include "cli_app.h"
#include "cli_log.h"
#include "command_collector.h"
#include "console_learning.h"
#include "console_ui.h"
#include "diag_commands.h"
#include "flash_commands.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/core/settings.h"
#include "hio/device/command_history.h"
#include "hio/device/command_tree_cache.h"
#include "hio/device/completer.h"
#include "hio/device/console_session.h"
#include "hio/device/device_info.h"
#include "hio/device/device_session.h"
#include "hio/device/profile.h"
#include "hio/device/session_registry.h"
#include "hio/device/shell_query.h"
#include "hio/hw/jlink/rtt_channel.h"
#include "jlink_commands.h"
#include "output.h"
#include "probe_options.h"
#include "target_commands.h"
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
    bool noSuggestions = false; // plan 0021: overrides console/suggestions for one run
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
        // Plan 0021 (ADR-0170): the console settings shared with the GUI.
        auto settings = Settings::openDefault();
        ui.suggestions =
            !options.noSuggestions && (!settings || settings->get(device::suggestionsSetting));
    }
    std::optional<ConsoleLearning> learning;
    if (interactive) {
        auto settings = Settings::openDefault();
        learning.emplace(**session, profileId, targetId,
                         !settings || settings->get(device::readCommandsOnConnectSetting),
                         device::CommandTreeCache::defaultDirectory());
        ui.learner = &learning->learner();
    }
    const Status status = runConsole(consoleContext, **session, view, ui);
    learning.reset();
    (*session)->close(); // writes the console file's trailer and flushes it
    view.printStopped();
    if (!status) {
        return std::unexpected(withReason(status.error()));
    }
    return CommandResult{.data = QJsonValue(), .humanText = powerCycleTip, .streamed = true};
}

// ---- command --------------------------------------------------------------------------------

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

    const Status status =
        runCommand(context, device, collector, splitLines(options.command),
                   CommandTiming{.writeTimeout = jlink::RttTiming{}.writeTimeout,
                                 .collect = std::chrono::milliseconds(
                                     static_cast<std::int64_t>(options.timeoutSeconds * 1000.0)),
                                 .endAtPrompt = false,
                                 .writeTimeoutCode = ErrorCode::TargetRttWriteTimeout});
    device.close();
    if (!status) {
        return std::unexpected(status.error());
    }
    return commandResult(context, options.command, collector.response());
}

// ---- info (plan 0021, ADR-0166, ADR-0175) -----------------------------------------------------

struct InfoOptions {
    ProbeOptions probe;
};

// `info show` through a silent ShellQuery; the parsed fields (fw-name, fw-version, ...).
Result<CommandResult> info(const Context& context, const GlobalOptions& globals,
                           const ProbeOptions& group, const InfoOptions& options) {
    auto location = locateJLinkLibrary(globals);
    if (!location) {
        return std::unexpected(std::move(location).error());
    }
    device::SessionRegistry registry;
    auto sessionOpts = sessionOptions(group, options.probe, chesterApp().defaultLatencyMs, false,
                                      false, {}, false);
    if (!sessionOpts) {
        return std::unexpected(std::move(sessionOpts).error());
    }
    const std::optional<int> speed = sessionOpts->speedKhz;
    warnAboutSlowSpeed(context, speed);
    auto session = device::DeviceSession::open(registry, *location, std::move(*sessionOpts));
    if (!session) {
        return std::unexpected(std::move(session).error());
    }
    device::DeviceSession& device = **session;
    if (const Status connected =
            device::waitUntilConnected(device, device::connectTimeoutForSpeed(speed), context.stop);
        !connected) {
        device.close();
        return std::unexpected(connected.error());
    }
    // Through the queue: it waits until the shell is idle (review P1).
    device::ShellQueryQueue queries;
    queries.setSession(&device);
    const std::optional<device::ShellQueryResult> answer = device::runAndWait(queries, "info show");
    queries.setSession(nullptr);
    device.close();
    const device::DeviceInfo parsed =
        device::parseInfoShow(answer ? answer->lines : std::vector<std::string>{});
    if (parsed.fields.empty()) {
        Error error = makeError(ErrorCode::Timeout, "info show: no fields in the answer");
        error.message = "The device did not answer `info show`.";
        error.cause = "The firmware has no `info show` command, or the shell is busy.";
        error.suggestion = "Check the console with `hio chester app console`.";
        return std::unexpected(std::move(error));
    }
    QJsonObject data;
    std::string human;
    for (const device::DeviceInfoField& field : parsed.fields) {
        data.insert(toQString(field.key), toQString(field.value));
        human += (human.empty() ? "" : "\n") + field.key + ": " + field.value;
    }
    return CommandResult{.data = data, .humanText = std::move(human), .streamed = false};
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
    consoleCommand->add_flag("--no-suggestions", consoleOptions->noSuggestions,
                             "Interactive mode: no hints from the command history (overrides the "
                             "setting console/suggestions for this run)");
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

    // Plan 0021: info (`info show` parsed).
    auto infoOptions = std::make_shared<InfoOptions>();
    CLI::App* infoCommand = appGroup->add_subcommand(
        "info", "Show what the device reports with `info show` (firmware name and version, ...)");
    addProbeOptions(*infoCommand, infoOptions->probe);
    infoCommand->callback([&slot, &globals, group, infoOptions] {
        slot.command = [&globals, group, infoOptions](const Context& context) {
            return info(context, globals, *group, *infoOptions);
        };
    });

    // M2: flash, erase, reset, recover, pib read, uicr read; the `chester lte` group.
    registerAppFlashCommands(*appGroup, group, slot, globals);
    // M4 (plan/features/0007): the live connection check.
    registerCheckCommand(*appGroup, group, slot, globals, "app");
    // M6 (plan/features/0013): status, halt, go, regs, zones, mem read|write, flash-write.
    registerTargetCommands(*appGroup, group, slot, globals, "app");
    registerLteCommands(*chester, slot, globals);
}

} // namespace hio::cli
