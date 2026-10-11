#pragma once

// `hio chester app console` and `hio chester app command` (plan/features/0002-m1-jlink-rtt.md,
// py-hardwario compatible options).

#include "cli_app.h"

#include <CLI/App.hpp>

namespace hio::cli {

void registerChesterCommands(CLI::App& app, CommandSlot& slot, const GlobalOptions& globals);

} // namespace hio::cli
