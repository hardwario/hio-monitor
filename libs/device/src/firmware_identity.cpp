#include "hwio/device/firmware_identity.h"

#include "hwio/core/firmware_identity.h"
#include "hwio/device/device_info.h"
#include "hwio/device/line_assembler.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace hwio::device {

namespace {

bool isAsciiLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// At most `max` bytes, not ending inside a UTF-8 character.
std::string cutUtf8(std::string_view text, std::size_t max) {
    if (text.size() <= max) {
        return std::string(text);
    }
    std::size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U) {
        --cut;
    }
    return std::string(text.substr(0, cut));
}

std::string_view trim(std::string_view text, std::string_view characters) {
    const std::size_t first = text.find_first_not_of(characters);
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(characters);
    return text.substr(first, last - first + 1);
}

bool equalsIgnoringCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::ranges::equal(a, b, [](char x, char y) { return lower(x) == lower(y); });
}

} // namespace

std::optional<FirmwareIdentity> identityFromInfo(const DeviceInfo& info) {
    if (!info.hasFirmware()) {
        return std::nullopt;
    }
    return FirmwareIdentity{.name = cutUtf8(info.fwName(), FirmwareIdentity::maxNameBytes),
                            .version = cutUtf8(info.fwVersion(), FirmwareIdentity::maxVersionBytes),
                            .deviceSerial =
                                cutUtf8(info.deviceSerial(), FirmwareIdentity::maxSerialBytes)};
}

std::optional<FirmwareIdentity> identityFromBanner(std::string_view line) {
    constexpr std::string_view head = "*** Booting ";
    constexpr std::string_view tail = " ***";
    const std::string cleaned = stripAnsiCsi(line);
    const std::string_view text = trim(cleaned, " \t\r\n");
    if (!text.starts_with(head) || !text.ends_with(tail) ||
        text.size() < head.size() + tail.size()) {
        return std::nullopt;
    }
    const std::string_view middle =
        trim(text.substr(head.size(), text.size() - head.size() - tail.size()), " ");
    const std::size_t space = middle.rfind(' ');
    if (space == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view name = trim(middle.substr(0, space), " ");
    const std::string_view version = middle.substr(space + 1);
    if (name.empty() || name.size() > FirmwareIdentity::maxNameBytes || version.size() < 2 ||
        version.size() > FirmwareIdentity::maxVersionBytes || version.front() != 'v' ||
        !isDigit(version[1])) {
        return std::nullopt;
    }
    constexpr std::array<std::string_view, 4> sdkNames{"Zephyr OS", "Zephyr OS build",
                                                       "nRF Connect SDK", "MCUboot"};
    if (std::ranges::any_of(
            sdkNames, [name](std::string_view sdk) { return equalsIgnoringCase(name, sdk); })) {
        return std::nullopt; // the SDK banner names no application
    }
    return FirmwareIdentity{
        .name = std::string(name), .version = std::string(version), .deviceSerial = {}};
}

bool sameFirmware(const FirmwareIdentity& a, const FirmwareIdentity& b) noexcept {
    return a.name == b.name && a.version == b.version;
}

bool otherDevice(const FirmwareIdentity& a, const FirmwareIdentity& b) noexcept {
    return !a.deviceSerial.empty() && !b.deviceSerial.empty() && a.deviceSerial != b.deviceSerial;
}

std::string identityText(const FirmwareIdentity& identity) {
    return identity.name + ' ' + identity.version;
}

std::string fileSlug(std::string_view text, std::size_t maxBytes) {
    std::string slug;
    bool dash = false;
    for (const char c : text) {
        if (isAsciiLetter(c) || isDigit(c)) {
            if (dash && !slug.empty()) {
                slug += '-';
            }
            dash = false;
            slug += lower(c);
        } else {
            dash = true;
        }
        if (slug.size() >= maxBytes) {
            break;
        }
    }
    slug.resize(std::min(slug.size(), maxBytes));
    return std::string(trim(slug, "-"));
}

std::string versionSlug(std::string_view text, std::size_t maxBytes) {
    std::string slug;
    bool dash = false;
    for (std::size_t i = 0; i < text.size() && slug.size() < maxBytes; ++i) {
        const char c = text[i];
        const bool keep = isAsciiLetter(c) || isDigit(c) || c == '+' || c == '_' || c == '-' ||
                          (c == '.' && i + 1 < text.size() && isDigit(text[i + 1]));
        if (!keep) {
            dash = true;
            continue;
        }
        if (dash && !slug.empty() && slug.back() != '-') {
            slug += '-';
        }
        dash = false;
        slug += c;
    }
    slug.resize(std::min(slug.size(), maxBytes));
    return std::string(trim(trim(slug, "-."), "-"));
}

std::string consoleFileBase(std::string_view profileId, std::string_view targetId,
                            const std::optional<FirmwareIdentity>& identity) {
    std::string base = std::string(profileId) + '-' + std::string(targetId);
    if (!identity) {
        return base;
    }
    std::string slug = fileSlug(identity->name);
    const std::string prefix = fileSlug(profileId) + '-';
    if (slug.starts_with(prefix) && slug.size() > prefix.size()) {
        slug.erase(0, prefix.size());
    }
    const std::string version = versionSlug(identity->version);
    if (slug.empty() || version.empty()) {
        return base;
    }
    return base + '-' + slug + '-' + version;
}

} // namespace hwio::device
