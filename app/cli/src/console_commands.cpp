#include "console_commands.h"

#include "cli_app.h"
#include "confirm.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/command_history.h"

#include <QJsonObject>
#include <QString>
#include <QStringLiteral>

#include <CLI/CLI.hpp>

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace hwio::cli {

namespace {

Result<CommandResult> clearHistory(const Context& context, bool yes) {
    // Decided 2026-11-02: asked on a TTY, --yes otherwise (ADR-0038 pattern).
    if (const Status confirmed = confirmDestructive(
            context, yes,
            "The saved command history of all devices (GUI and CLI) will be deleted.\n"
            "Continue? [y/N] ");
        !confirmed) {
        return std::unexpected(confirmed.error());
    }
    // The directory of CommandHistory::defaultFile() (the GUI uses the same one).
    const std::filesystem::path directory =
        device::CommandHistory::defaultFile("profile", "target").parent_path();
    if (const Status cleared = device::CommandHistory::clearAll(directory); !cleared) {
        return std::unexpected(cleared.error());
    }
    QJsonObject data;
    data.insert(QStringLiteral("directory"), toQString(toUtf8(directory)));
    return CommandResult{.data = data,
                         .humanText = "The saved command history of all devices was deleted.",
                         .streamed = false,
                         .failure = std::nullopt};
}

} // namespace

void registerConsoleCommands(CLI::App& app, CommandSlot& slot) {
    CLI::App* group = app.add_subcommand("console", "Console settings and data");
    group->require_subcommand(1);
    CLI::App* history = group->add_subcommand("history", "The saved command history");
    history->require_subcommand(1);
    CLI::App* clear = history->add_subcommand(
        "clear", "Delete the saved command history of all devices (GUI and CLI)");
    auto yes = std::make_shared<bool>(false);
    clear->add_flag("--yes", *yes, "Do not ask for confirmation");
    clear->callback([&slot, yes] {
        slot.command = [yes](const Context& context) { return clearHistory(context, *yes); };
    });
}

} // namespace hwio::cli
