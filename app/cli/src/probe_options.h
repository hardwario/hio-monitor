#pragma once

// -n/--jlink-sn and --jlink-speed of the `chester app` / `chester lte` groups and their
// subcommands (py-hardwario compatible; the subcommand value wins only when it is given - the
// py-hardwario bug that overwrote the group value is not reproduced).

#include "hio/core/result.h"

#include <CLI/App.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace hio::cli {

struct ProbeOptions {
    std::optional<std::uint32_t> serialNumber;
    std::optional<std::string> speed; // --jlink-speed as given: "auto" or kHz (checked when parsed)
};

void addProbeOptions(CLI::App& command, ProbeOptions& options);

[[nodiscard]] std::optional<std::uint32_t> serialNumberOf(const ProbeOptions& group,
                                                          const ProbeOptions& own);
// The subcommand's value wins; without any, `defaultSpeedKhz` (nullopt = automatic, ADR-0090).
[[nodiscard]] Result<std::optional<int>> speedOf(const ProbeOptions& group, const ProbeOptions& own,
                                                 std::optional<int> defaultSpeedKhz);

} // namespace hio::cli
