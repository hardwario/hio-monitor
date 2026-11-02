#include "hwio/device/device_info.h"

#include "hwio/device/line_assembler.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hwio::device {

namespace {

bool isKeyChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '_' || c == ' ';
}

// D2 (plan 0028): [A-Za-z0-9 _-], spaces only single and between words.
bool isKey(std::string_view key) {
    return !key.empty() && std::ranges::all_of(key, isKeyChar) && key.front() != ' ' &&
           key.back() != ' ' && key.find("  ") == std::string_view::npos;
}

// Lower-case, ' ' -> '-' ("firmware name" -> "firmware-name").
std::string normalisedKey(std::string_view key) {
    std::string result(key);
    for (char& c : result) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        } else if (c == ' ') {
            c = '-';
        }
    }
    return result;
}

// The first present (not "(unset)", not empty) value of `keys`.
std::string_view firstValue(const DeviceInfo& info, std::initializer_list<std::string_view> keys) {
    constexpr std::string_view unset = "(unset)";
    for (const std::string_view key : keys) {
        const std::optional<std::string_view> value = info.value(key);
        if (value && !value->empty() && *value != unset) {
            return *value;
        }
    }
    return {};
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

std::string_view DeviceInfo::fwName() const noexcept {
    return firstValue(*this, {"fw-name", "firmware-name"});
}

std::string_view DeviceInfo::fwVersion() const noexcept {
    return firstValue(*this, {"fw-version", "firmware-version"});
}

std::string_view DeviceInfo::deviceSerial() const noexcept {
    return firstValue(*this, {"serial-number"});
}

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
        const std::string_view rawKey = line.substr(0, colon);
        if (!isKey(rawKey)) {
            continue; // text, not a field
        }
        const std::string key = normalisedKey(rawKey);
        std::string_view value = trim(line.substr(colon + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        if (info.value(key)) {
            continue; // the first occurrence wins
        }
        info.fields.push_back(DeviceInfoField{
            .key = key, .value = std::string(cutUtf8(value, DeviceInfo::maxFieldBytes))});
    }
    return info;
}

} // namespace hwio::device
