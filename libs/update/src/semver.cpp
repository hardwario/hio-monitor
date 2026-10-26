#include "hio/update/semver.h"

#include "hio/core/error.h"
#include "hio/core/result.h"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hio::update {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isIdentifierChar(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
}

bool isNumeric(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, isDigit);
}

Error invalid(std::string_view text, std::string_view why) {
    return makeError(ErrorCode::UsageInvalidArgument,
                     std::format("'{}' is not a semantic version: {}", text, why));
}

// A numeric part: digits, no leading zero (except "0"), fits in 32 bits.
std::optional<std::uint32_t> parseNumber(std::string_view text) {
    if (!isNumeric(text) || (text.size() > 1 && text.front() == '0')) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char digit : text) {
        value = (value * 10) + static_cast<std::uint64_t>(digit - '0');
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
    }
    return static_cast<std::uint32_t>(value);
}

std::vector<std::string_view> split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find(separator, start);
        if (end == std::string_view::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

// Numeric identifiers compare by value; they have no leading zeros, so by length, then by text.
std::strong_ordering compareNumeric(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return a.size() <=> b.size();
    }
    return a.compare(b) <=> 0;
}

std::strong_ordering compareIdentifier(std::string_view a, std::string_view b) {
    const bool aNumeric = isNumeric(a);
    const bool bNumeric = isNumeric(b);
    if (aNumeric && bNumeric) {
        return compareNumeric(a, b);
    }
    if (aNumeric != bNumeric) {
        return aNumeric ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    return a.compare(b) <=> 0;
}

} // namespace

Result<SemVer> SemVer::parse(std::string_view text) {
    std::string_view rest = text;
    if (rest.starts_with('v')) {
        rest.remove_prefix(1);
    }
    SemVer version;
    if (const std::size_t plus = rest.find('+'); plus != std::string_view::npos) {
        const std::string_view build = rest.substr(plus + 1);
        for (const std::string_view identifier : split(build, '.')) {
            if (identifier.empty() || !std::ranges::all_of(identifier, isIdentifierChar)) {
                return std::unexpected(invalid(text, "invalid build metadata"));
            }
        }
        version.build = std::string(build);
        rest = rest.substr(0, plus);
    }
    if (const std::size_t dash = rest.find('-'); dash != std::string_view::npos) {
        for (const std::string_view identifier : split(rest.substr(dash + 1), '.')) {
            if (identifier.empty() || !std::ranges::all_of(identifier, isIdentifierChar)) {
                return std::unexpected(invalid(text, "invalid pre-release identifier"));
            }
            if (isNumeric(identifier) && identifier.size() > 1 && identifier.front() == '0') {
                return std::unexpected(invalid(text, "numeric identifier with a leading zero"));
            }
            version.prerelease.emplace_back(identifier);
        }
        rest = rest.substr(0, dash);
    }
    const std::vector<std::string_view> core = split(rest, '.');
    if (core.size() != 3) {
        return std::unexpected(invalid(text, "expected MAJOR.MINOR.PATCH"));
    }
    const auto major = parseNumber(core.at(0));
    const auto minor = parseNumber(core.at(1));
    const auto patch = parseNumber(core.at(2));
    if (!major || !minor || !patch) {
        return std::unexpected(invalid(text, "invalid MAJOR.MINOR.PATCH number"));
    }
    version.major = *major;
    version.minor = *minor;
    version.patch = *patch;
    return version;
}

std::strong_ordering SemVer::operator<=>(const SemVer& other) const {
    if (const auto order = major <=> other.major; order != 0) {
        return order;
    }
    if (const auto order = minor <=> other.minor; order != 0) {
        return order;
    }
    if (const auto order = patch <=> other.patch; order != 0) {
        return order;
    }
    if (prerelease.empty() != other.prerelease.empty()) {
        // A normal version has a higher precedence than a pre-release of it.
        return prerelease.empty() ? std::strong_ordering::greater : std::strong_ordering::less;
    }
    const std::size_t common = std::min(prerelease.size(), other.prerelease.size());
    for (std::size_t i = 0; i < common; ++i) {
        if (const auto order = compareIdentifier(prerelease.at(i), other.prerelease.at(i));
            order != 0) {
            return order;
        }
    }
    return prerelease.size() <=> other.prerelease.size();
}

bool SemVer::operator==(const SemVer& other) const { return (*this <=> other) == 0; }

std::string SemVer::toString(bool withBuild) const {
    std::string text = std::format("{}.{}.{}", major, minor, patch);
    for (std::size_t i = 0; i < prerelease.size(); ++i) {
        text += (i == 0 ? '-' : '.');
        text += prerelease.at(i);
    }
    if (withBuild && !build.empty()) {
        text += '+';
        text += build;
    }
    return text;
}

bool isDevelopmentBuild(const SemVer& version) {
    return std::ranges::find(version.prerelease, std::string("dev")) != version.prerelease.end();
}

SemVer comparableCurrent(const SemVer& version) {
    if (!isDevelopmentBuild(version)) {
        return version;
    }
    return SemVer{.major = version.major,
                  .minor = version.minor,
                  .patch = version.patch,
                  .prerelease = {},
                  .build = {}};
}

} // namespace hio::update
