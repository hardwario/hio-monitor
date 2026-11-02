#pragma once

// DeviceInfo - the fields of the CHESTER shell command `info show` (plan/features/0021, ADR-0166):
// "<key>: <value>" lines. Real CHESTER firmware (chester-sdk ctr_info_shell.c) prints keys with
// spaces such as "firmware name: CHESTER Clime" and "firmware version: v3.0.0"; older fixtures
// used "fw-name: hio-chester-push-fm" (both accepted, plan 0028 D2).
//
// parseInfoShow() is pure and bounded: at most maxFields fields; keys of at most maxKeyBytes
// characters [A-Za-z0-9 _-] with single spaces only between words (no leading or trailing
// space), stored normalised: lower-case, spaces as '-' ("firmware name" -> "firmware-name");
// values cut to maxFieldBytes bytes (at a UTF-8 character boundary); one pair of surrounding
// double quotes removed; the first occurrence of a key wins; every other line (the echoed command,
// the prompt, empty lines, text) is ignored. A line is split at its first ':'. The query that
// produces the lines (ShellQuery) is plan 0021 phase 2.

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hwio::device {

struct DeviceInfoField {
    std::string key;
    std::string value;
};

struct DeviceInfo {
    static constexpr std::size_t maxFields = 64;
    static constexpr std::size_t maxFieldBytes = 256;
    static constexpr std::size_t maxKeyBytes = 64;

    std::vector<DeviceInfoField> fields; // in output order, <= maxFields

    // The value of `key`, nullopt when the output had no such field.
    [[nodiscard]] std::optional<std::string_view> value(std::string_view key) const noexcept;
    // "fw-name", else "firmware-name" / "fw-version", else "firmware-version" / "serial-number"
    // ("" when missing; chester-sdk's "(unset)" counts as missing).
    [[nodiscard]] std::string_view fwName() const noexcept;
    [[nodiscard]] std::string_view fwVersion() const noexcept;
    [[nodiscard]] std::string_view deviceSerial() const noexcept;
    // Both the firmware name and version are known (enough to show and to key the command tree
    // cache).
    [[nodiscard]] bool hasFirmware() const noexcept;
};

[[nodiscard]] DeviceInfo parseInfoShow(std::span<const std::string> lines);

} // namespace hwio::device
