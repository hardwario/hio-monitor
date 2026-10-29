#pragma once

// `hwio chester app console` and `hwio chester app command` (plan/features/0002-m1-jlink-rtt.md,
// py-hardwario compatible options).

#include "cli_app.h"
#include "hwio/core/result.h"

#include <CLI/App.hpp>

#include <optional>
#include <string_view>

namespace hwio::cli {

// --jlink-speed: "auto" (nullopt: the automatic speed, as pylink; ADR-0090) or a fixed speed in
// kHz, device::minSpeedKhz..device::maxSpeedKhz. Errors: usage.invalid_argument (English text).
[[nodiscard]] Result<std::optional<int>> parseJLinkSpeed(std::string_view text);

void registerChesterCommands(CLI::App& app, CommandSlot& slot, const GlobalOptions& globals);

} // namespace hwio::cli
