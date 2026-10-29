#pragma once

// Plan 0025 (ADR-0186): the CLI counterpart of Settings > Console > "Clear history":
//   hwio console history clear [--json]
// Deletes every saved command history file (history-*.txt in the console directory, the files of
// all devices and profiles, GUI and CLI alike). English only (ADR-0027).

#include "cli_app.h"

#include <CLI/App.hpp>

namespace hwio::cli {

void registerConsoleCommands(CLI::App& app, CommandSlot& slot);

} // namespace hwio::cli
