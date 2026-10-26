#pragma once

// M7 update check (plan/features/0016 B1, ADR-0116, ADR-0122):
//   hio update check [--prerelease] [--json]
// Asks the GitHub release list once (no automatic check in the CLI). Exit code 0 when up to date
// and when a newer version is available; errors (update.*, cancelled) use the standard rendering.
// English only (ADR-0027).

#include "cli_app.h"

#include <CLI/App.hpp>

namespace hio::cli {

void registerUpdateCommands(CLI::App& app, CommandSlot& slot);

} // namespace hio::cli
