#pragma once

// DeviceInfo - the fields of the CHESTER shell command `info show` (plan/features/0021, ADR-0166):
// "<key>: <value>" lines such as "fw-name: hio-chester-push-fm" and "fw-version: v4.0.1".
//
// parseInfoShow() is pure and bounded: at most maxFields fields; keys of at most maxKeyBytes
// characters [A-Za-z0-9_-]; values cut to maxFieldBytes bytes (at a UTF-8 character boundary);
// one pair of surrounding double quotes removed; the first occurrence of a key wins; every other
// line (the echoed command, the prompt, empty lines, text) is ignored. The query that produces the
// lines (ShellQuery) is plan 0021 phase 2.

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hio::device {

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
    // "fw-name" / "fw-version" ("" when missing).
    [[nodiscard]] std::string_view fwName() const noexcept;
    [[nodiscard]] std::string_view fwVersion() const noexcept;
    // Both fw-name and fw-version are known (enough to show and to key the command tree cache).
    [[nodiscard]] bool hasFirmware() const noexcept;
};

[[nodiscard]] DeviceInfo parseInfoShow(std::span<const std::string> lines);

} // namespace hio::device
