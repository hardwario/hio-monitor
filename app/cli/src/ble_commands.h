#pragma once

// `hio ble scan|console|command|pair` (ADR-0085, plan/features/0011-m5-ble-backend-cli.md).
// English only (ADR-0027). Every command owns its BleHost (ADR-0080) for its run.

#include "cli_app.h"

#include <CLI/App.hpp>

namespace hio::cli {

void registerBleCommands(CLI::App& app, CommandSlot& slot, const GlobalOptions& globals);

} // namespace hio::cli
