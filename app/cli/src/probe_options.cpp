#include "probe_options.h"

#include "chester_commands.h"
#include "hio/core/result.h"
#include "hio/device/device_session.h"

#include <CLI/CLI.hpp>

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace hio::cli {

void addProbeOptions(CLI::App& command, ProbeOptions& options) {
    command.add_option("-n,--jlink-sn", options.serialNumber, "J-Link serial number")
        ->option_text("SERIAL_NUMBER");
    command
        .add_option("--jlink-speed", options.speed,
                    std::format("J-Link clock speed: auto (automatic, as pylink) or a fixed speed "
                                "in kHz ({}-{}) [auto]",
                                device::minSpeedKhz, device::maxSpeedKhz))
        ->check(CLI::Validator(
            [](const std::string& value) {
                auto speed = parseJLinkSpeed(value);
                return speed ? std::string() : std::move(speed).error().message;
            },
            "auto|KHZ"))
        ->option_text("auto|KHZ");
}

std::optional<std::uint32_t> serialNumberOf(const ProbeOptions& group, const ProbeOptions& own) {
    return own.serialNumber ? own.serialNumber : group.serialNumber;
}

Result<std::optional<int>> speedOf(const ProbeOptions& group, const ProbeOptions& own,
                                   std::optional<int> defaultSpeedKhz) {
    if (const auto& given = own.speed ? own.speed : group.speed) {
        return parseJLinkSpeed(*given);
    }
    return defaultSpeedKhz;
}

} // namespace hio::cli
