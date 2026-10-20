#include "ble_commands.h"

#include "cli_app.h"
#include "cli_log.h"
#include "command_collector.h"
#include "console_ui.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/ble_lock.h"
#include "hio/device/ble_session.h"
#include "hio/device/command_history.h"
#include "hio/device/console_session.h"
#include "hio/device/probe_lock.h"
#include "hio/device/profile.h"
#include "hio/device/session_registry.h"
#include "hio/hw/ble/ble_host.h"
#include "hio/hw/ble/ble_scanner.h"
#include "hio/hw/ble/nus_link.h"
#include "hio/hw/ble/types.h"
#include "output.h"
#include "terminal.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QtCore/qnamespace.h>
#include <QtCore/qtconfigmacros.h>
#include <QtGlobal>

#if QT_CONFIG(permissions)
#include <QPermissions>
#endif

#include <CLI/CLI.hpp>

#include <atomic>
#include <chrono>
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
#include <variant>
#include <vector>

namespace hio::cli {

namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

constexpr std::string_view defaultProfile = "chester";
// `--name`: one scan of this length (ADR-0085).
constexpr std::chrono::milliseconds nameScan{5000};
// Safety bound of the CLI's wait for a connection: the session's own deadlines are 30 s to the
// CCCD write (HIL H3), then 60 s while pairing (Q6), plus the up to 3 s wait for an earlier
// controller of the device (H2) and a margin.
constexpr std::chrono::milliseconds connectSafetyTimeout{95000};
// `ble pair`: BlueZ may connect first (as long as a connect, H3), then waits for the passkey
// (60 s, ADR-0084), plus a margin.
constexpr std::chrono::milliseconds pairTimeout{95000};
constexpr std::chrono::milliseconds adapterTimeout{5000};
// B8: the user answers the system's permission prompt.
constexpr std::chrono::milliseconds permissionTimeout{120000};

// An unknown or empty value in a human table.
constexpr std::string_view unknown = "—"; // em dash

struct DeviceOptions {
    std::string address;
    std::string name;
    std::string profile{defaultProfile};
};

struct ScanCommandOptions {
    double timeoutSeconds = 5.0;
    bool all = false;
    std::string profile{defaultProfile};
};

struct ConsoleOptions {
    DeviceOptions device;
    bool autoReconnect = false;
    std::string historyFile;
    std::string consoleFile;
    bool consoleFileGiven = false;
};

struct CommandOptions {
    DeviceOptions device;
    double timeoutSeconds = 3.0;
    std::string consoleFile;
    bool consoleFileGiven = false;
    std::string command;
};

Error usage(std::string message, std::string detail = {}) {
    Error error = makeError(ErrorCode::UsageInvalidArgument, std::move(detail));
    error.message = std::move(message);
    return error;
}

// The profile of --profile; it must have a Bluetooth console. Errors: usage.invalid_argument.
Result<const device::DeviceProfile*> bleProfile(const std::string& id) {
    const device::DeviceProfile* profile = device::ProfileRegistry::find(id);
    if (profile == nullptr) {
        return std::unexpected(usage(std::format("Unknown device profile \"{}\".", id), id));
    }
    if (!profile->ble) {
        return std::unexpected(
            usage(std::format("The device profile \"{}\" has no Bluetooth console.", id), id));
    }
    return profile;
}

// Scan rows of `profile` only (ADR-0083, ADR-0142: by the name pattern).
ble::ProfileMatcher matcherFor(const device::DeviceProfile& profile) {
    return [&profile](const ble::Advertisement& advertisement) {
        const device::DeviceProfile* found = device::ProfileRegistry::findByAdvertisement(
            advertisement.name, advertisement.serviceUuids);
        return found == &profile ? std::string(profile.id) : std::string();
    };
}

// One scan; the devices sorted by RSSI. Errors: the adapter's, cancelled.
Result<std::vector<ble::BleDeviceInfo>> scanOnce(const Context& context, ble::BleHost& host,
                                                 ble::ScanOptions options) {
    ble::BleScanner scanner(host);
    bool finished = false;
    std::optional<Error> failure;
    QObject::connect(&scanner, &ble::BleScanner::finished, &scanner,
                     [&](const std::optional<Error>& error) {
                         finished = true;
                         failure = error;
                     });
    const auto duration = options.duration;
    if (Status started = scanner.start(std::move(options)); !started) {
        return std::unexpected(std::move(started).error());
    }
    const Status waited =
        runEventsUntil([&finished] { return finished; }, duration + 5s, context.stop);
    scanner.stop();
    if (!waited) {
        return std::unexpected(waited.error());
    }
    if (failure) {
        return std::unexpected(*failure);
    }
    return scanner.devices();
}

// Review B8 (plan 0011 decisions): asks the system for the Bluetooth permission on this, the main
// thread, before the first scan or connect when it was not decided yet (macOS shows its prompt;
// other systems answer at once). Errors: ble.permission_denied, timeout, cancelled.
Status requestBluetoothPermission(const Context& context) {
#if QT_CONFIG(permissions)
    if (QCoreApplication::instance() == nullptr) {
        return {};
    }
    qCInfo(lcCli) << "Requesting the Bluetooth permission";
    QBluetoothPermission permission;
    permission.setCommunicationModes(QBluetoothPermission::Access);
    std::optional<Qt::PermissionStatus> status;
    const QObject receiver;
    QCoreApplication::instance()->requestPermission(
        permission, &receiver, [&status](const QPermission& result) { status = result.status(); });
    if (Status waited = runEventsUntil([&status] { return status.has_value(); }, permissionTimeout,
                                       context.stop);
        !waited) {
        return waited;
    }
    if (!status) {
        return std::unexpected(
            makeError(ErrorCode::Timeout, "no answer to the permission request"));
    }
    if (*status == Qt::PermissionStatus::Denied) {
        return std::unexpected(makeError(ErrorCode::BlePermissionDenied, "denied when asked"));
    }
#else
    static_cast<void>(context);
#endif
    return {};
}

// The adapter, ready to use; an undecided permission is asked for first (B8). Errors:
// ble.adapter_*, ble.permission_denied, ble.stack_unavailable, timeout, cancelled.
Result<ble::AdapterInfo> readyAdapter(const Context& context, ble::BleHost& host) {
    auto adapter = host.queryAdapter(adapterTimeout);
    if (!adapter) {
        return std::unexpected(std::move(adapter).error());
    }
    if (adapter->state == ble::AdapterState::PermissionUndetermined) {
        if (Status asked = requestBluetoothPermission(context); !asked) {
            return std::unexpected(std::move(asked).error());
        }
        // Review N6: granted - the adapter itself may still be off or missing.
        adapter = host.queryAdapter(adapterTimeout);
        if (!adapter) {
            return std::unexpected(std::move(adapter).error());
        }
    }
    if (auto problem = ble::adapterError(*adapter)) {
        return std::unexpected(std::move(*problem));
    }
    return adapter;
}

struct ResolvedDevice {
    ble::BleDeviceId id;
    std::string name; // "" when given by address
};

// -a ID, or --name N: a 5 s scan, a case-insensitive substring of the advertised name among the
// profile's devices; exactly one must match. Errors: ble.device_not_found,
// usage.invalid_argument (several matches), the adapter's.
Result<ResolvedDevice> resolveDevice(const Context& context, ble::BleHost& host,
                                     const DeviceOptions& options,
                                     const device::DeviceProfile& profile) {
    if (!options.address.empty()) {
        return ResolvedDevice{.id = ble::canonicalDeviceId(options.address), .name = {}}; // B6
    }
    if (context.mode == OutputMode::Human) {
        std::cerr << std::format("Scanning for \"{}\"...\n", options.name);
    }
    auto devices = scanOnce(
        context, host,
        ble::ScanOptions{.duration = nameScan, .matcher = matcherFor(profile), .all = false});
    if (!devices) {
        return std::unexpected(std::move(devices).error());
    }
    const std::string wanted = ble::toLowerAscii(options.name);
    std::vector<ble::BleDeviceInfo> matches;
    for (const ble::BleDeviceInfo& info : *devices) {
        if (ble::toLowerAscii(info.name).find(wanted) != std::string::npos) {
            matches.push_back(info);
        }
    }
    if (matches.empty()) {
        Error error = makeError(ErrorCode::BleDeviceNotFound, options.name);
        error.message = std::format("The Bluetooth device {} was not found.", options.name);
        return std::unexpected(std::move(error));
    }
    if (matches.size() > 1) {
        std::string ids;
        for (const ble::BleDeviceInfo& info : matches) {
            ids += std::format("{}{} ({})", ids.empty() ? "" : ", ", info.id, info.name);
        }
        return std::unexpected(
            usage(std::format("Several Bluetooth devices match \"{}\": {}. Use --address.",
                              options.name, ids),
                  ids));
    }
    return ResolvedDevice{.id = matches.front().id, .name = matches.front().name};
}

std::string describe(const ResolvedDevice& device) {
    return device.name.empty() ? device.id : std::format("{} ({})", device.name, device.id);
}

QJsonValue ownerJson(const std::optional<device::ProbeLockOwner>& owner) {
    if (!owner) {
        return {QJsonValue::Null};
    }
    QJsonObject object;
    object.insert(QStringLiteral("app"), toQString(owner->app));
    object.insert(QStringLiteral("pid"), static_cast<double>(owner->pid));
    return object;
}

// ---- scan -------------------------------------------------------------------------------------

Result<CommandResult> scan(const Context& context, const ScanCommandOptions& options) {
    auto profile = bleProfile(options.profile);
    if (!profile) {
        return std::unexpected(std::move(profile).error());
    }
    ble::BleHost host;
    auto adapter = readyAdapter(context, host);
    if (!adapter) {
        return std::unexpected(std::move(adapter).error());
    }
    const auto duration =
        std::chrono::milliseconds(static_cast<std::int64_t>(options.timeoutSeconds * 1000.0));
    auto devices =
        scanOnce(context, host,
                 ble::ScanOptions{
                     .duration = duration, .matcher = matcherFor(**profile), .all = options.all});
    if (!devices) {
        return std::unexpected(std::move(devices).error());
    }
    const fs::path lockDirectory = device::BleLock::defaultDirectory();
    QJsonArray rowsJson;
    std::vector<std::vector<std::string>> rows;
    for (const ble::BleDeviceInfo& info : *devices) {
        const auto owner = device::BleLock::owner(info.id, lockDirectory);
        QJsonObject row;
        row.insert(QStringLiteral("id"), toQString(info.id));
        row.insert(QStringLiteral("name"), toQString(info.name));
        row.insert(QStringLiteral("rssi"), info.rssi);
        row.insert(QStringLiteral("nusAdvertised"), info.nusAdvertised);
        row.insert(QStringLiteral("profile"), info.profileId.empty()
                                                  ? QJsonValue(QJsonValue::Null)
                                                  : QJsonValue(toQString(info.profileId)));
        row.insert(QStringLiteral("inUseBy"), ownerJson(owner));
        rowsJson.append(row);
        rows.push_back({info.id, info.name.empty() ? std::string(unknown) : info.name,
                        std::to_string(info.rssi),
                        info.profileId.empty() ? std::string(unknown) : info.profileId,
                        owner ? std::format("{}, PID {}", owner->app, owner->pid) : std::string()});
    }
    QJsonObject adapterJson;
    adapterJson.insert(QStringLiteral("name"), toQString(adapter->name));
    adapterJson.insert(QStringLiteral("address"), toQString(adapter->address));
    QJsonObject data;
    data.insert(QStringLiteral("adapter"), adapterJson);
    data.insert(QStringLiteral("devices"), rowsJson);

    std::string text;
    if (!rows.empty()) {
        text += formatTable({"ID", "NAME", "RSSI", "PROFILE", "IN USE"}, rows);
    }
    const auto seconds = std::chrono::duration<double>(duration).count();
    text += std::format("{} {} ({:g} s scan).", rows.size(),
                        rows.size() == 1 ? "device" : "devices", seconds);
    if (!options.all) {
        text += " Use --all to show every Bluetooth device.";
    }
    return CommandResult{.data = data, .humanText = text, .streamed = false};
}

// ---- console and command ---------------------------------------------------------------------

// The pairing prompt (ADR-0084): on stderr in human mode, logged only with --json (Q8).
void showPairingPrompt(const Context& context, const device::DeviceProfile& profile,
                       const std::function<void(std::string_view)>& print) {
    std::string text = "Pairing: enter the passkey in the system dialog.";
    if (const QString hint = device::passkeyHint(profile); !hint.isEmpty()) {
        text += " " + hint.toStdString();
    }
    qCInfo(lcCli).noquote() << toQString(text);
    if (context.mode == OutputMode::Human) {
        print(text);
    }
}

std::optional<fs::path> consoleFileOf(const std::string& path, bool given) {
    if (!given) {
        return std::nullopt;
    }
    return fs::path(toQString(path).toStdU16String()); // '' disables the console file
}

Result<CommandResult> console(const Context& context, const ConsoleOptions& options) {
    auto profile = bleProfile(options.device.profile);
    if (!profile) {
        return std::unexpected(std::move(profile).error());
    }
    ble::BleHost host; // outlives the registry and the session (declared first)
    if (auto adapter = readyAdapter(context, host); !adapter) { // B8: before the first connect
        return std::unexpected(std::move(adapter).error());
    }
    auto resolved = resolveDevice(context, host, options.device, **profile);
    if (!resolved) {
        return std::unexpected(std::move(resolved).error());
    }
    device::SessionRegistry registry;
    auto session = device::BleSession::open(
        registry, host,
        device::BleSessionOptions{.profile = *profile,
                                  .deviceId = resolved->id,
                                  .deviceName = resolved->name,
                                  .autoReconnect = options.autoReconnect,
                                  .consoleFile =
                                      consoleFileOf(options.consoleFile, options.consoleFileGiven),
                                  .consoleLog = nullptr});
    if (!session) {
        return std::unexpected(std::move(session).error());
    }
    device::BleSession& ble = **session;
    const bool interactive =
        context.mode == OutputMode::Human && stdinIsTerminal() && stdoutIsTerminal();
    // The console's own stop request: Ctrl+C / SIGTERM or a closed stdout (review R32).
    std::stop_source consoleStop;
    const std::stop_callback forwardStop(context.stop,
                                         [&consoleStop] { consoleStop.request_stop(); });
    Context consoleContext = context;
    consoleContext.stop = consoleStop.get_token();
    ConsoleView view(consoleContext, ble, interactive && stdoutColors(), false);
    view.setOutputClosedHandler([&consoleStop] { consoleStop.request_stop(); });
    const auto withReason = [&view](Error error) {
        if (error.code == ErrorCode::Cancelled && view.outputClosed()) {
            return outputClosedError("the standard output was closed");
        }
        return error;
    };
    QObject::connect(&ble, &device::BleSession::pairingPrompt, &ble, [&] {
        showPairingPrompt(context, **profile,
                          [&view](std::string_view text) { view.printStatus(text); });
    });
    view.printStatus(std::format("Connecting to {}...", describe(*resolved)));
    if (const Status connected = waitForConsoleConnection(
            consoleContext, ble, view, options.autoReconnect, connectSafetyTimeout);
        !connected) {
        return std::unexpected(withReason(connected.error()));
    }
    ConsoleUiOptions ui;
    ui.interactive = interactive;
    ui.prompt = std::string((*profile)->ble.value_or(device::BleProfile{}).shellPrompt);
    if (interactive) {
        ui.historyFile =
            options.historyFile.empty()
                ? device::CommandHistory::defaultFile((*profile)->id, ble.consoleTargetId())
                : fs::path(toQString(options.historyFile).toStdU16String());
    }
    const Status status = runConsole(consoleContext, ble, view, ui);
    ble.close(); // writes the console file's last line and closes it
    view.printStopped();
    if (!status) {
        return std::unexpected(withReason(status.error()));
    }
    return CommandResult{.data = QJsonValue(), .humanText = {}, .streamed = true};
}

Result<CommandResult> command(const Context& context, const CommandOptions& options) {
    auto profile = bleProfile(options.device.profile);
    if (!profile) {
        return std::unexpected(std::move(profile).error());
    }
    ble::BleHost host;
    if (auto adapter = readyAdapter(context, host); !adapter) { // B8: before the first connect
        return std::unexpected(std::move(adapter).error());
    }
    auto resolved = resolveDevice(context, host, options.device, **profile);
    if (!resolved) {
        return std::unexpected(std::move(resolved).error());
    }
    device::SessionRegistry registry;
    auto session = device::BleSession::open(
        registry, host,
        device::BleSessionOptions{.profile = *profile,
                                  .deviceId = resolved->id,
                                  .deviceName = resolved->name,
                                  .autoReconnect = false,
                                  .consoleFile =
                                      consoleFileOf(options.consoleFile, options.consoleFileGiven),
                                  .consoleLog = nullptr});
    if (!session) {
        return std::unexpected(std::move(session).error());
    }
    device::BleSession& ble = **session;
    ResponseCollector collector(context, ble);
    QObject::connect(&ble, &device::BleSession::pairingPrompt, &ble, [&] {
        showPairingPrompt(context, **profile,
                          [](std::string_view text) { std::cerr << text << '\n'; });
    });
    if (const Status connected =
            device::waitUntilConnected(ble, connectSafetyTimeout, context.stop);
        !connected) {
        ble.close();
        return std::unexpected(connected.error());
    }
    // The M1 semantics (plan 0011): the timeout counts from the last echo; the prompt after it
    // ends the collection early.
    const Status status =
        runCommand(context, ble, collector, splitLines(options.command),
                   CommandTiming{.writeTimeout = ble::LinkTiming{}.writeTimeout,
                                 .collect = std::chrono::milliseconds(
                                     static_cast<std::int64_t>(options.timeoutSeconds * 1000.0)),
                                 .endAtPrompt = true,
                                 .writeTimeoutCode = ErrorCode::BleWriteTimeout});
    ble.close();
    if (!status) {
        return std::unexpected(status.error());
    }
    return commandResult(context, options.command, collector.response());
}

// ---- pair -----------------------------------------------------------------------------------

Result<CommandResult> pair(const Context& context, const DeviceOptions& options) {
    auto profile = bleProfile(options.profile);
    if (!profile) {
        return std::unexpected(std::move(profile).error());
    }
    ble::BleHost host;
    if (auto adapter = readyAdapter(context, host); !adapter) {
        return std::unexpected(std::move(adapter).error());
    }
    auto resolved = resolveDevice(context, host, options, **profile);
    if (!resolved) {
        return std::unexpected(std::move(resolved).error());
    }
    // Q4: the device must not be used by another program meanwhile (exit 5).
    auto lock = device::BleLock::acquire(resolved->id, device::BleLock::defaultDirectory());
    if (!lock) {
        return std::unexpected(std::move(lock).error());
    }
    std::atomic<bool> wakePending{false};
    QObject receiver;
    auto subscription = ble::BleHost::subscribe([&wakePending, &receiver] {
        if (!wakePending.exchange(true)) {
            QMetaObject::invokeMethod(&receiver, [] {}, Qt::QueuedConnection);
        }
    });
    if (Status posted = host.post(ble::PairDevice{
            .device = resolved->id, .subscriber = subscription, .client = std::nullopt});
        !posted) {
        subscription->detach();
        return std::unexpected(std::move(posted).error());
    }
    if (context.mode == OutputMode::Human) {
        std::cerr << std::format("Pairing with {}...\n", describe(*resolved));
    }
    std::optional<ble::PairingResult> result;
    const Status waited = runEventsUntil(
        [&] {
            wakePending.store(false);
            while (auto event = subscription->pop()) {
                if (auto* pairing = std::get_if<ble::PairingResult>(&*event)) {
                    result = std::move(*pairing);
                }
            }
            return result.has_value();
        },
        pairTimeout, context.stop);
    subscription->detach();
    if (!waited) {
        return std::unexpected(waited.error());
    }
    if (!result) {
        return std::unexpected(makeError(ErrorCode::Timeout, "no pairing result"));
    }
    if (!result->status) {
        return std::unexpected(result->status.error());
    }
    QJsonObject data;
    data.insert(QStringLiteral("id"), toQString(resolved->id));
    data.insert(QStringLiteral("paired"), true);
    return CommandResult{.data = data,
                         .humanText = result->alreadyPaired ? "Already paired." : "Paired.",
                         .streamed = false};
}

// -a/--address ID | --name N: exactly one of them.
void addDeviceOptions(CLI::App& command, DeviceOptions& options) {
    CLI::Option_group* device = command.add_option_group("device", "The device (one of them)");
    device
        ->add_option("-a,--address", options.address,
                     "Bluetooth address (macOS: the device UUID shown by `hio ble scan`)")
        ->option_text("ID");
    device
        ->add_option("--name", options.name,
                     "A part of the advertised name (case-insensitive); exactly one device found "
                     "by a 5 s scan must match")
        ->option_text("N");
    device->require_option(1);
}

} // namespace

void registerBleCommands(CLI::App& app, CommandSlot& slot, const GlobalOptions& /*globals*/) {
    CLI::App* ble = app.add_subcommand("ble", "Bluetooth LE devices (console over NUS)");
    ble->require_subcommand(1);

    // scan
    auto scanOptions = std::make_shared<ScanCommandOptions>();
    CLI::App* scanCommand = ble->add_subcommand("scan", "List nearby Bluetooth devices");
    scanCommand->add_option("--timeout", scanOptions->timeoutSeconds, "Scan duration in seconds")
        ->capture_default_str()
        ->check(CLI::Range(1.0, 60.0))
        ->option_text("S");
    scanCommand->add_flag("--all", scanOptions->all,
                          "Show every Bluetooth device, not only those of the profile");
    scanCommand->add_option("--profile", scanOptions->profile, "Device profile")
        ->capture_default_str()
        ->option_text("ID");
    scanCommand->callback([&slot, scanOptions] {
        slot.command = [scanOptions](const Context& context) {
            return scan(context, *scanOptions);
        };
    });

    // console
    auto consoleOptions = std::make_shared<ConsoleOptions>();
    CLI::App* consoleCommand =
        ble->add_subcommand("console", "Start the interactive console over Bluetooth");
    addDeviceOptions(*consoleCommand, consoleOptions->device);
    consoleCommand->add_option("--profile", consoleOptions->device.profile, "Device profile")
        ->capture_default_str()
        ->option_text("ID");
    consoleCommand->add_flag("--auto-reconnect", consoleOptions->autoReconnect,
                             "Reconnect after the link drops while the device is in range, "
                             "instead of leaving the console disconnected until F4");
    consoleCommand
        ->add_option("--history-file", consoleOptions->historyFile,
                     "Command history file (default: in the application data)")
        ->option_text("PATH");
    CLI::Option* consoleFile = consoleCommand
                                   ->add_option("--console-file", consoleOptions->consoleFile,
                                                "Console log file ('' = none; default: one file "
                                                "per session in the application data)")
                                   ->option_text("PATH");
    consoleCommand->callback([&slot, consoleOptions, consoleFile] {
        consoleOptions->consoleFileGiven = consoleFile->count() > 0;
        slot.command = [consoleOptions](const Context& context) {
            return console(context, *consoleOptions);
        };
    });

    // command
    auto commandOptions = std::make_shared<CommandOptions>();
    CLI::App* commandCommand =
        ble->add_subcommand("command", "Send a command over Bluetooth and print the response");
    addDeviceOptions(*commandCommand, commandOptions->device);
    commandCommand
        ->add_option("-t,--timeout", commandOptions->timeoutSeconds,
                     "Seconds to collect the response after sending (the prompt ends it earlier)")
        ->capture_default_str()
        ->check(CLI::Range(0.001, 3600.0))
        ->option_text("TIMEOUT");
    CLI::Option* commandConsoleFile =
        commandCommand
            ->add_option("--console-file", commandOptions->consoleFile,
                         "Console log file ('' = none; default: one file per run in the "
                         "application data)")
            ->option_text("PATH");
    commandCommand->add_option("command", commandOptions->command, "Shell command(s), one per line")
        ->required();
    commandCommand->callback([&slot, commandOptions, commandConsoleFile] {
        commandOptions->consoleFileGiven = commandConsoleFile->count() > 0;
        slot.command = [commandOptions](const Context& context) {
            return command(context, *commandOptions);
        };
    });

    // pair
    auto pairOptions = std::make_shared<DeviceOptions>();
    CLI::App* pairCommand = ble->add_subcommand(
        "pair", "Pair a Bluetooth device (the system asks for the passkey; not on macOS)");
    addDeviceOptions(*pairCommand, *pairOptions);
    pairCommand->callback([&slot, pairOptions] {
        slot.command = [pairOptions](const Context& context) {
            return pair(context, *pairOptions);
        };
    });
}

} // namespace hio::cli
