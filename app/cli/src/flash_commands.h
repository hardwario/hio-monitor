#pragma once

// M2 commands (plan/features/0003-m2-flashing.md, F2-F4):
//   hio chester app flash [HEX_FILE_OR_ID] [--halt] [--allow-uicr] [--no-cache]
//   hio chester app erase [--all] [--yes] | reset [--halt] | recover [--yes]
//   hio chester app pib read | uicr read --format hex|bin FILE
//   hio chester lte flash FILE [--allow-uicr] | erase [--yes] | reset | recover [--yes]
// Each opens a session with attachRtt = false on the command's target and runs one operation
// (operation_runner.h); progress through ProgressUi, confirmations through confirmDestructive
// (ADR-0038).

#include "cli_app.h"
#include "hio/core/result.h"
#include "probe_options.h"

#include <CLI/App.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace hio::cli {

void registerAppFlashCommands(CLI::App& appGroup, const std::shared_ptr<ProbeOptions>& group,
                              CommandSlot& slot, const GlobalOptions& globals);
void registerLteCommands(CLI::App& chester, CommandSlot& slot, const GlobalOptions& globals);

// `chester app flash` argument rules (exposed for tests): an existing path is a file, 32 hex
// digits a catalog ID, anything else usage.invalid_argument; no argument: the first non-empty of
// build/ then build/zephyr/ x merged.hex, zephyr.hex, merged_chester_nrf52840.hex under
// `directory` (usage.invalid_argument listing the searched paths when none exists).
struct FlashFile {
    std::filesystem::path path;
};
struct FlashCatalogId {
    std::string id;
};
using FlashInput = std::variant<FlashFile, FlashCatalogId>;
[[nodiscard]] Result<FlashInput> resolveFlashInput(const std::string& argument,
                                                   const std::filesystem::path& directory);

} // namespace hio::cli
