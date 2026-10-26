#include "hio/hw/jlink/types.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

namespace hio::jlink {

namespace {

constexpr std::string_view compiledMarker = "compiled";

// "Sep 21 2023 10:38:43" (any amount of spaces between the fields), as a sortable tuple.
using DateKey = std::tuple<int, int, int, int, int, int>;

std::optional<int> parseNumber(std::string_view text) {
    if (text.empty() || text.size() > 4) {
        return std::nullopt;
    }
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = (value * 10) + (c - '0');
    }
    return value;
}

std::optional<DateKey> parseCompileDate(std::string_view firmware) {
    const std::size_t marker = firmware.find(compiledMarker);
    if (marker == std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view rest = firmware.substr(marker + compiledMarker.size());
    std::array<std::string_view, 4> fields; // month, day, year, time
    for (std::string_view& field : fields) {
        const std::size_t begin = rest.find_first_not_of(' ');
        if (begin == std::string_view::npos) {
            return std::nullopt;
        }
        rest = rest.substr(begin);
        const std::size_t end = std::min(rest.find(' '), rest.size());
        field = rest.substr(0, end);
        rest = rest.substr(end);
    }
    static constexpr std::array<std::string_view, 12> months{
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    // std::array's iterator is a pointer in libstdc++ and libc++ but a class in the MSVC STL
    // (`auto*` does not compile there).
    // NOLINTNEXTLINE(readability-qualified-auto)
    const auto month = std::ranges::find(months, fields[0]);
    if (month == months.end()) {
        return std::nullopt;
    }
    const std::string_view time = fields[3];
    if (time.size() != 8 || time[2] != ':' || time[5] != ':') {
        return std::nullopt;
    }
    const auto day = parseNumber(fields[1]);
    const auto year = parseNumber(fields[2]);
    const auto hour = parseNumber(time.substr(0, 2));
    const auto minute = parseNumber(time.substr(3, 2));
    const auto second = parseNumber(time.substr(6, 2));
    if (!day || !year || !hour || !minute || !second) {
        return std::nullopt;
    }
    return DateKey{*year, static_cast<int>(month - months.begin()), *day, *hour, *minute, *second};
}

} // namespace

std::string formatLibraryVersion(std::uint32_t version) {
    const std::uint32_t major = version / 10000;
    const std::uint32_t minor = (version / 100) % 100;
    const std::uint32_t revision = version % 100;
    std::string text = std::format("{}.{:02}", major, minor);
    if (revision > 0 && revision <= 26) {
        text += static_cast<char>('a' + revision - 1);
    } else if (revision > 26) {
        text += std::format(".{}", revision);
    }
    return text;
}

std::string_view connectionId(Connection connection) noexcept {
    return connection == Connection::Ip ? "ip" : "usb";
}

std::string_view firmwareStatusId(FirmwareStatus status) noexcept {
    switch (status) {
    case FirmwareStatus::Current:
        return "current";
    case FirmwareStatus::Outdated:
        return "outdated";
    case FirmwareStatus::Newer:
        return "newer";
    case FirmwareStatus::Unknown:
        return "unknown";
    }
    return "current";
}

std::optional<FirmwareStatus> compareFirmware(std::string_view probeFirmware,
                                              std::string_view libraryFirmware) {
    const auto probeDate = parseCompileDate(probeFirmware);
    const auto libraryDate = parseCompileDate(libraryFirmware);
    if (!probeDate || !libraryDate) {
        return std::nullopt;
    }
    if (*libraryDate > *probeDate) {
        return FirmwareStatus::Outdated;
    }
    // pylink firmware_newer(): not outdated, but a different firmware string.
    return probeFirmware != libraryFirmware ? FirmwareStatus::Newer : FirmwareStatus::Current;
}

std::string firmwareId(std::string_view firmware) {
    return std::string(firmware.substr(0, firmware.find(compiledMarker)));
}

} // namespace hio::jlink
