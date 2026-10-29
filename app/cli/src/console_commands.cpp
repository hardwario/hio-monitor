#include "console_commands.h"

#include "cli_app.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/command_history.h"

#include <QJsonObject>
#include <QString>
#include <QStringLiteral>

#include <CLI/CLI.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace hwio::cli {

namespace {

Result<CommandResult> clearHistory() {
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
    clear->callback(
        [&slot] { slot.command = [](const Context& /*context*/) { return clearHistory(); }; });
}

} // namespace hwio::cli
