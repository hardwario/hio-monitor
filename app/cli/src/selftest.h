#pragma once

// Hidden `_selftest` commands (not shown in --help) that exercise error rendering, exit codes and
// cancellation end to end:
//   hio _selftest error <id>   returns the error with that id from hio::errorTable
//                              (unknown id -> usage.invalid_argument)
//   hio _selftest wait         blocks until cancelled (Ctrl+C / SIGTERM) -> cancelled, exit 130

#include "cli_app.h"

#include <CLI/App.hpp>

namespace hio::cli {

void registerSelftestCommands(CLI::App& app, CommandSlot& slot);

} // namespace hio::cli
