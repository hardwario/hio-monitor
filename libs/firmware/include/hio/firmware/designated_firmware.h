#pragma once

// The designated firmware of a target (ADR-0165, plan/features/0021-gui-feedback-round-1.md) and
// SHA-256 helpers shared by the designated download and the catalog checksum (ADR-0166).
//
// A designated firmware is compiled into the device profile: display name, version, HTTPS URL, the
// SHA-256 of the file and the kind it must classify as (ADR-0040). FirmwareCatalog downloads it,
// verifies the SHA-256 before the file is cached or used, and re-verifies every cache hit.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hio::firmware {

// A SHA-256 digest (32 bytes).
using Sha256 = std::array<std::uint8_t, 32>;

// What a firmware file is (the ADR-0040 classification of an LTE input; an application HEX).
enum class FirmwareKind : std::uint8_t {
    AppHex,    // an Intel HEX file
    LteBundle, // zip with modem.zip + application.hex
    ModemOnly, // Nordic modem firmware zip
};

struct DesignatedFirmware {
    std::string_view name;    // display name, not translated ("HARDWARIO LTE firmware")
    std::string_view version; // "1.7.0"
    std::string_view url;     // HTTPS download URL
    Sha256 sha256{};          // of the downloaded file
    FirmwareKind kind = FirmwareKind::LteBundle;
};

namespace detail {
// Not constexpr: reached only from sha256FromHex() with an invalid literal, which then fails to
// compile ("not a constant expression").
void invalidSha256Literal();

consteval int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    invalidSha256Literal();
    return 0;
}
} // namespace detail

// A SHA-256 from 64 hex digits at compile time (profile data); anything else does not compile.
consteval Sha256 sha256FromHex(std::string_view hex) {
    if (hex.size() != 64) {
        detail::invalidSha256Literal();
    }
    Sha256 digest{};
    for (std::size_t i = 0; i < digest.size(); ++i) {
        digest.at(i) = static_cast<std::uint8_t>((detail::hexValue(hex.at(2 * i)) * 16) +
                                                 detail::hexValue(hex.at((2 * i) + 1)));
    }
    return digest;
}

// 64 hex digits (either case) -> digest; nullopt otherwise.
[[nodiscard]] std::optional<Sha256> parseSha256(std::string_view hex) noexcept;
// Lowercase hex (64 digits).
[[nodiscard]] std::string toHex(const Sha256& digest);
// SHA-256 of `bytes` (QCryptographicHash).
[[nodiscard]] Sha256 sha256Of(std::span<const std::byte> bytes);

} // namespace hio::firmware
