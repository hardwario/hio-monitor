#include "hio/device/device_info.h"

#include "hio/device/line_assembler.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hio::device {

namespace {

bool isKeyChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '_';
}

std::string_view trim(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

// At most `max` bytes, not ending inside a UTF-8 character.
std::string_view cutUtf8(std::string_view text, std::size_t max) {
    if (text.size() <= max) {
        return text;
    }
    std::size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U) {
        --cut;
    }
    return text.substr(0, cut);
}

} // namespace

std::optional<std::string_view> DeviceInfo::value(std::string_view key) const noexcept {
    const auto it = std::ranges::find(fields, key, &DeviceInfoField::key);
    if (it == fields.end()) {
        return std::nullopt;
    }
    return std::string_view(it->value);
}

std::string_view DeviceInfo::fwName() const noexcept { return value("fw-name").value_or(""); }

std::string_view DeviceInfo::fwVersion() const noexcept { return value("fw-version").value_or(""); }

bool DeviceInfo::hasFirmware() const noexcept { return !fwName().empty() && !fwVersion().empty(); }

DeviceInfo parseInfoShow(std::span<const std::string> lines) {
    DeviceInfo info;
    for (const std::string& raw : lines) {
        if (info.fields.size() >= DeviceInfo::maxFields) {
            break;
        }
        const std::string cleaned = stripAnsiCsi(raw);
        const std::string_view line = trim(cleaned);
        if (line.find(":~$") != std::string_view::npos) {
            continue; // a Zephyr prompt ("uart:~$ info show")
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0 || colon > DeviceInfo::maxKeyBytes) {
            continue;
        }
        const std::string_view key = line.substr(0, colon);
        if (!std::ranges::all_of(key, isKeyChar)) {
            continue; // text, not a field
        }
        std::string_view value = trim(line.substr(colon + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        if (info.value(key)) {
            continue; // the first occurrence wins
        }
        info.fields.push_back(
            DeviceInfoField{.key = std::string(key),
                            .value = std::string(cutUtf8(value, DeviceInfo::maxFieldBytes))});
    }
    return info;
}

} // namespace hio::device
