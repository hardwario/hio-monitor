#include "hio/firmware/designated_firmware.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QtTypes>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hio::firmware {

namespace {

std::optional<int> hexDigit(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return std::nullopt;
}

} // namespace

std::optional<Sha256> parseSha256(std::string_view hex) noexcept {
    Sha256 digest{};
    if (hex.size() != digest.size() * 2) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < digest.size(); ++i) {
        const std::optional<int> high = hexDigit(hex[2 * i]);
        const std::optional<int> low = hexDigit(hex[(2 * i) + 1]);
        if (!high || !low) {
            return std::nullopt;
        }
        digest.at(i) = static_cast<std::uint8_t>((*high * 16) + *low);
    }
    return digest;
}

std::string toHex(const Sha256& digest) {
    static constexpr std::string_view digits = "0123456789abcdef";
    std::string text;
    text.reserve(digest.size() * 2);
    for (const std::uint8_t value : digest) {
        text += digits[value >> 4U];
        text += digits[value & 0x0FU];
    }
    return text;
}

Sha256 sha256Of(std::span<const std::byte> bytes) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayView(bytes.data(), static_cast<qsizetype>(bytes.size())));
    const QByteArray result = hash.result();
    Sha256 digest{};
    std::ranges::transform(result.first(static_cast<qsizetype>(digest.size())), digest.begin(),
                           [](char c) { return static_cast<std::uint8_t>(c); });
    return digest;
}

} // namespace hio::firmware
