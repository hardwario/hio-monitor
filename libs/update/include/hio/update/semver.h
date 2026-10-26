#pragma once

// Semantic versions (SemVer 2.0.0, https://semver.org) for the update check (plan/features/0016,
// ADR-0116).
//
// parse() accepts the strict SemVer 2.0.0 grammar with an optional leading 'v' ("v2.1.0-rc.1"):
// numeric parts without leading zeros (each <= 2^32 - 1), non-empty dot-separated pre-release
// and build identifiers of [0-9A-Za-z-], numeric pre-release identifiers without leading zeros.
// Precedence (operator<=>, operator==) follows SemVer 2.0.0 section 11: major, minor, patch, then
// a version with pre-release identifiers is lower than the same version without; identifiers are
// compared one by one (numeric < alphanumeric, numbers by value, text by ASCII order, a shorter
// list is lower when all its identifiers are equal). Build metadata is kept but ignored.
//
// Development builds (cmake/HioVersion.cmake: "2.0.1-dev.5+abc1234", "2.0.0-rc.1.dev.5+abc1234",
// "2.0.0-dev+abc1234") have a "dev" pre-release identifier; the update check compares them by
// their numeric part (comparableCurrent()).

#include "hio/core/result.h"

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hio::update {

struct SemVer {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
    std::vector<std::string> prerelease; // dot-separated identifiers
    std::string build;                   // without the '+'

    // Errors: usage.invalid_argument (the text in `detail`).
    [[nodiscard]] static Result<SemVer> parse(std::string_view text);

    [[nodiscard]] std::strong_ordering operator<=>(const SemVer& other) const;
    // Precedence equality: build metadata is ignored.
    [[nodiscard]] bool operator==(const SemVer& other) const;

    [[nodiscard]] bool isPrerelease() const noexcept { return !prerelease.empty(); }
    // "2.1.0-rc.1" (no 'v'; with "+build" when `withBuild`).
    [[nodiscard]] std::string toString(bool withBuild = false) const;
};

// The version has a "dev" pre-release identifier (an untagged development build).
[[nodiscard]] bool isDevelopmentBuild(const SemVer& version);

// The version the update check compares with: development builds by their numeric part
// (major.minor.patch), every other version unchanged.
[[nodiscard]] SemVer comparableCurrent(const SemVer& version);

} // namespace hio::update
