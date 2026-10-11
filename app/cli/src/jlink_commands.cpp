#include "jlink_commands.h"

#include "cli_app.h"
#include "cli_log.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/core/settings.h"
#include "hio/device/probe_lock.h"
#include "hio/device/probe_tools.h"
#include "hio/device/session_registry.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/types.h"
#include "output.h"
#include "terminal.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QString>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::cli {

namespace {

namespace fs = std::filesystem;

// An unknown or empty value in a human table.
constexpr std::string_view unknown = "\u2014"; // em dash

QJsonValue ownerJson(const std::optional<device::ProbeLockOwner>& owner) {
    if (!owner) {
        return {QJsonValue::Null};
    }
    QJsonObject object;
    object.insert(QStringLiteral("app"), toQString(owner->app));
    object.insert(QStringLiteral("pid"), static_cast<double>(owner->pid));
    return object;
}

// Characters (UTF-8 code points) of `text`, for column alignment.
std::size_t displayWidth(std::string_view text) {
    return static_cast<std::size_t>(std::ranges::count_if(
        text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U; }));
}

std::string padded(std::string text, std::size_t width) {
    const std::size_t current = displayWidth(text);
    if (current < width) {
        text.append(width - current, ' ');
    }
    return text;
}

// A table with columns two spaces apart (widths in characters).
std::string formatTable(const std::vector<std::string>& header,
                        const std::vector<std::vector<std::string>>& rows) {
    std::vector<std::size_t> widths;
    for (std::size_t column = 0; column < header.size(); ++column) {
        std::size_t width = displayWidth(header[column]);
        for (const auto& row : rows) {
            width = std::max(width, displayWidth(row[column]));
        }
        widths.push_back(width + 2);
    }
    const auto formatRow = [&](const std::vector<std::string>& row) {
        std::string line;
        for (std::size_t column = 0; column < row.size(); ++column) {
            line += column + 1 < row.size() ? padded(row[column], widths[column]) : row[column];
        }
        return line + "\n";
    };
    std::string text = formatRow(header);
    for (const auto& row : rows) {
        text += formatRow(row);
    }
    return text;
}

Result<CommandResult> listProbes(const GlobalOptions& globals) {
    auto location = locateJLinkLibrary(globals);
    if (!location) {
        return std::unexpected(std::move(location).error());
    }
    device::SessionRegistry registry;
    auto list = registry.enumerateProbes(*location);
    if (!list) {
        return std::unexpected(std::move(list).error());
    }
    QJsonArray probes;
    std::vector<std::vector<std::string>> rows;
    for (const device::ProbeStatus& status : list->probes) {
        QJsonObject probe;
        probe.insert(QStringLiteral("serialNumber"),
                     static_cast<double>(status.probe.serialNumber));
        probe.insert(QStringLiteral("product"), toQString(status.probe.product));
        probe.insert(QStringLiteral("connection"),
                     toQString(jlink::connectionId(status.probe.connection)));
        // `list` never opens a probe: the firmware is known only when the library lists it
        // (null otherwise; `hio jlink info` shows it - decided 2026-10-09).
        probe.insert(QStringLiteral("firmware"),
                     status.probe.firmware.empty() ? QJsonValue(QJsonValue::Null)
                                                   : QJsonValue(toQString(status.probe.firmware)));
        probe.insert(QStringLiteral("inUseBy"), ownerJson(status.usedByOtherProcess));
        probes.append(probe);
        const std::string inUse = status.usedByOtherProcess
                                      ? std::format("{}, PID {}", status.usedByOtherProcess->app,
                                                    status.usedByOtherProcess->pid)
                                      : std::string(unknown);
        rows.push_back(
            {std::to_string(status.probe.serialNumber),
             status.probe.product.empty() ? std::string(unknown) : status.probe.product,
             std::string(jlink::connectionId(status.probe.connection)),
             status.probe.firmware.empty() ? std::string(unknown) : status.probe.firmware, inUse});
    }
    QJsonObject data;
    data.insert(QStringLiteral("library"), libraryJson(*location, list->libraryVersion));
    data.insert(QStringLiteral("probes"), probes);

    std::string text =
        std::format("J-Link library {} ({})\n", jlink::formatLibraryVersion(list->libraryVersion),
                    toUtf8(location->path));
    if (rows.empty()) {
        text += "No J-Link probe found.";
    } else {
        text += formatTable({"Serial number", "Product", "Connection", "Firmware", "In use"}, rows);
    }
    return CommandResult{.data = data, .humanText = text, .streamed = false};
}

Result<CommandResult> probeInfo(const GlobalOptions& globals, std::optional<std::uint32_t> sn) {
    auto location = locateJLinkLibrary(globals);
    if (!location) {
        return std::unexpected(std::move(location).error());
    }
    const device::SessionRegistry registry;
    auto details = device::inspectProbe(registry, *location, sn);
    if (!details) {
        return std::unexpected(std::move(details).error());
    }
    QJsonObject data;
    data.insert(QStringLiteral("library"), libraryJson(*location, details->libraryVersion));
    data.insert(QStringLiteral("serialNumber"), static_cast<double>(details->probe.serialNumber));
    data.insert(QStringLiteral("product"), toQString(details->probe.product));
    data.insert(QStringLiteral("connection"),
                toQString(jlink::connectionId(details->probe.connection)));
    data.insert(QStringLiteral("firmware"), toQString(details->probe.firmware));
    data.insert(QStringLiteral("libraryFirmware"), toQString(details->libraryFirmware));
    data.insert(QStringLiteral("firmwareStatus"),
                toQString(jlink::firmwareStatusId(details->firmwareStatus)));
    data.insert(QStringLiteral("targetMillivolts"), details->targetMillivolts
                                                        ? QJsonValue(*details->targetMillivolts)
                                                        : QJsonValue(QJsonValue::Null));
    std::string text;
    text +=
        std::format("Library:          {} ({})\n",
                    jlink::formatLibraryVersion(details->libraryVersion), toUtf8(location->path));
    text += std::format("Serial number:    {}\n", details->probe.serialNumber);
    text +=
        std::format("Product:          {}\n",
                    details->probe.product.empty() ? std::string(unknown) : details->probe.product);
    text += std::format("Firmware:         {}\n", details->probe.firmware);
    text +=
        std::format("Firmware status:  {}{}\n", jlink::firmwareStatusId(details->firmwareStatus),
                    details->firmwareStatus == jlink::FirmwareStatus::Outdated ||
                            details->firmwareStatus == jlink::FirmwareStatus::Newer
                        ? " (run `hio jlink update-firmware`)"
                        : "");
    text += std::format("Target voltage:   {}",
                        details->targetMillivolts ? std::format("{} mV", *details->targetMillivolts)
                                                  : std::string("unknown"));
    return CommandResult{.data = data, .humanText = text, .streamed = false};
}

Result<CommandResult> updateFirmware(const Context& context, const GlobalOptions& globals,
                                     std::optional<std::uint32_t> sn, bool yes) {
    auto location = locateJLinkLibrary(globals);
    if (!location) {
        return std::unexpected(std::move(location).error());
    }
    if (!yes) {
        if (context.mode == OutputMode::Json || !stdinIsTerminal()) {
            return fail(ErrorCode::UsageConfirmationRequired);
        }
        const std::string probe = sn ? std::format("J-Link {}", *sn) : std::string("the J-Link");
        const auto answer = askYesNo(std::format(
            "Update the firmware of {} to the one bundled with the J-Link Software? Do not "
            "disconnect the probe during the update. [y/N] ",
            probe));
        if (!answer) {
            return fail(ErrorCode::Cancelled);
        }
    }
    const device::SessionRegistry registry;
    auto update = device::updateProbeFirmware(registry, *location, sn);
    if (!update) {
        return std::unexpected(std::move(update).error());
    }
    const bool updated = update->before != jlink::FirmwareStatus::Current;
    QJsonObject data;
    data.insert(QStringLiteral("serialNumber"), static_cast<double>(update->serialNumber));
    data.insert(QStringLiteral("before"), toQString(jlink::firmwareStatusId(update->before)));
    data.insert(QStringLiteral("updated"), updated);
    const std::string text =
        updated ? std::format("The firmware of J-Link {} was {}. The probe restarts; wait a few "
                              "seconds before using it.",
                              update->serialNumber,
                              update->before == jlink::FirmwareStatus::Newer ? "synchronized"
                                                                             : "updated")
                : std::format("The firmware of J-Link {} is up to date.", update->serialNumber);
    return CommandResult{.data = data, .humanText = text, .streamed = false};
}

} // namespace

Result<jlink::LibraryLocation> locateJLinkLibrary(const GlobalOptions& globals) {
    std::optional<fs::path> option;
    if (!globals.jlinkLibrary.empty()) {
        option = fs::path(toQString(globals.jlinkLibrary).toStdU16String());
    }
    auto settings = Settings::openDefault();
    if (!settings) {
        qCWarning(lcCli).noquote()
            << "Settings not available:" << toQString(settings.error().detail);
    }
    const jlink::LocatorOptions options =
        jlink::defaultLocatorOptions(std::move(option), settings ? &*settings : nullptr);
    return jlink::locateLibrary(options);
}

QJsonObject libraryJson(const jlink::LibraryLocation& location, std::uint32_t version) {
    QJsonObject library;
    library.insert(QStringLiteral("path"), toQString(toUtf8(location.path)));
    library.insert(QStringLiteral("version"), toQString(jlink::formatLibraryVersion(version)));
    return library;
}

void registerJLinkCommands(CLI::App& app, CommandSlot& slot, const GlobalOptions& globals) {
    CLI::App* jlink = app.add_subcommand("jlink", "SEGGER J-Link probes");
    jlink->require_subcommand(1);

    CLI::App* list = jlink->add_subcommand("list", "List the connected J-Link probes");
    list->callback([&slot, &globals] {
        slot.command = [&globals](const Context& /*context*/) { return listProbes(globals); };
    });

    auto infoSerial = std::make_shared<std::optional<std::uint32_t>>();
    CLI::App* info = jlink->add_subcommand("info", "Show a J-Link probe and its firmware status");
    info->add_option("-n,--jlink-sn", *infoSerial, "J-Link serial number (default: the first)")
        ->option_text("SERIAL_NUMBER");
    info->callback([&slot, &globals, infoSerial] {
        slot.command = [&globals, infoSerial](const Context& /*context*/) {
            return probeInfo(globals, *infoSerial);
        };
    });

    auto updateSerial = std::make_shared<std::optional<std::uint32_t>>();
    auto yes = std::make_shared<bool>(false);
    CLI::App* update = jlink->add_subcommand(
        "update-firmware", "Update the J-Link firmware to the one bundled with the library");
    update->add_option("-n,--jlink-sn", *updateSerial, "J-Link serial number (default: the first)")
        ->option_text("SERIAL_NUMBER");
    update->add_flag("--yes", *yes, "Do not ask for confirmation");
    update->callback([&slot, &globals, updateSerial, yes] {
        slot.command = [&globals, updateSerial, yes](const Context& context) {
            return updateFirmware(context, globals, *updateSerial, *yes);
        };
    });
}

} // namespace hio::cli
