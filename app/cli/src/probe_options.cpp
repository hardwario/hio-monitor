#include "probe_options.h"

#include <CLI/CLI.hpp>

#include <cstdint>
#include <format>
#include <optional>

namespace hio::cli {

void addProbeOptions(CLI::App& command, ProbeOptions& options, int defaultSpeedKhz) {
    command.add_option("-n,--jlink-sn", options.serialNumber, "J-Link serial number")
        ->option_text("SERIAL_NUMBER");
    command
        .add_option("--jlink-speed", options.speedKhz,
                    std::format("J-Link clock speed in kHz [{}]", defaultSpeedKhz))
        ->option_text("SPEED");
}

std::optional<std::uint32_t> serialNumberOf(const ProbeOptions& group, const ProbeOptions& own) {
    return own.serialNumber ? own.serialNumber : group.serialNumber;
}

int speedOf(const ProbeOptions& group, const ProbeOptions& own, int defaultSpeedKhz) {
    return own.speedKhz.value_or(group.speedKhz.value_or(defaultSpeedKhz));
}

} // namespace hio::cli
