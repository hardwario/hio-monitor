#pragma once

// -n/--jlink-sn and --jlink-speed of the `chester app` / `chester lte` groups and their
// subcommands (py-hardwario compatible; the subcommand value wins only when it is given - the
// py-hardwario bug that overwrote the group value is not reproduced).

#include <CLI/App.hpp>

#include <cstdint>
#include <optional>

namespace hio::cli {

struct ProbeOptions {
    std::optional<std::uint32_t> serialNumber;
    std::optional<int> speedKhz;
};

void addProbeOptions(CLI::App& command, ProbeOptions& options, int defaultSpeedKhz);

[[nodiscard]] std::optional<std::uint32_t> serialNumberOf(const ProbeOptions& group,
                                                          const ProbeOptions& own);
[[nodiscard]] int speedOf(const ProbeOptions& group, const ProbeOptions& own, int defaultSpeedKhz);

} // namespace hio::cli
