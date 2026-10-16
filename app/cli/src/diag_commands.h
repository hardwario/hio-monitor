#pragma once

// M4 diagnostics (plan/features/0007, ADR-0068, ADR-0070, ADR-0075):
//   hio doctor [--json]                         the environment checks
//   hio diag report [-o FILE] [--json]          the diagnostic report zip
//   hio chester app|lte check [-n SN] [--jlink-speed KHZ] [--json]
//                                               the live connection check (Diagnose, ADR-0069)
// Exit codes (ADR-0070): 0 when nothing failed, otherwise the category of the first failed check.

#include "cli_app.h"
#include "probe_options.h"

#include <CLI/App.hpp>

#include <memory>
#include <string_view>

namespace hio::cli {

void registerDiagCommands(CLI::App& app, CommandSlot& slot, const GlobalOptions& globals);

// `check` under the `chester app` / `chester lte` group (`group` holds the group's probe options).
void registerCheckCommand(CLI::App& targetGroup, const std::shared_ptr<ProbeOptions>& group,
                          CommandSlot& slot, const GlobalOptions& globals,
                          std::string_view targetId);

} // namespace hio::cli
