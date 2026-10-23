#include "hio/device/pib.h"

#include "hio/core/error.h"
#include "hio/core/result.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QString>
#include <QtTypes>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace hio::device {

namespace {

constexpr std::uint32_t signatureValue = 0xBABECAFE;
// py-hardwario PIB(version=2, nrf=True): 0x45 + 4 + 33 + 17; version 1: 0x58 + 4 + 33 + 17.
constexpr std::size_t sizeVersion2 = 0x45 + 4 + 33 + 17;
constexpr std::size_t sizeVersion1 = 0x58 + 4 + 33 + 17;

struct Field {
    std::size_t offset;
    std::size_t length;
};
constexpr Field vendorName{.offset = 0x06, .length = 17};
constexpr Field productName{.offset = 0x17, .length = 17};
constexpr Field hwVariant{.offset = 0x28, .length = 11};
constexpr Field hwRevision{.offset = 0x33, .length = 7};
constexpr Field serialNumber{.offset = 0x3A, .length = 11};
constexpr Field claimToken{.offset = 0x45, .length = 33};
constexpr Field blePasskey{.offset = 0x66, .length = 17};

std::uint32_t bigEndian32(std::span<const std::byte> bytes, std::size_t offset) {
    return (std::to_integer<std::uint32_t>(bytes[offset]) << 24U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 8U) |
           std::to_integer<std::uint32_t>(bytes[offset + 3]);
}

std::uint32_t littleEndian32(std::span<const std::byte> bytes, std::size_t offset) {
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

std::string ascii(std::span<const std::byte> bytes, const Field& field) {
    std::string text;
    for (std::size_t i = 0; i < field.length; ++i) {
        const auto value = std::to_integer<unsigned char>(bytes[field.offset + i]);
        text += value < 0x80 ? static_cast<char>(value) : '?';
    }
    while (!text.empty() && text.back() == '\0') {
        text.pop_back();
    }
    return text;
}

Error invalid(std::string_view check, std::string detail) {
    Error error = makeError(ErrorCode::PibInvalid, std::move(detail));
    error.message = QCoreApplication::translate(
                        "hio::device", "The Product Information Block is missing or damaged (%1).")
                        .arg(QString::fromUtf8(check.data(), static_cast<qsizetype>(check.size())))
                        .toStdString();
    return error;
}

} // namespace

std::uint32_t pibCrc32(std::span<const std::byte> bytes) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::byte byte : bytes) {
        crc ^= std::to_integer<std::uint32_t>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0 ? 0xEDB88320U : 0U);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

Result<Pib> decodePib(std::span<const std::byte, 128> bytes) {
    Pib pib;
    pib.version = std::to_integer<std::uint8_t>(bytes[4]);
    if (pib.version != 1 && pib.version != 2) {
        return std::unexpected(invalid("version", std::format("version 0x{:02x}", pib.version)));
    }
    pib.signature = pib.version == 1 ? littleEndian32(bytes, 0) : bigEndian32(bytes, 0);
    if (pib.signature != signatureValue) {
        return std::unexpected(
            invalid("signature", std::format("signature 0x{:08x}", pib.signature)));
    }
    // Version 1 keeps its size at 0x08 (16-bit little-endian), version 2 at 0x05.
    const std::size_t size = pib.version == 1 ? (std::to_integer<std::size_t>(bytes[8]) |
                                                 (std::to_integer<std::size_t>(bytes[9]) << 8U))
                                              : std::to_integer<std::size_t>(bytes[5]);
    const std::size_t expected = pib.version == 1 ? sizeVersion1 : sizeVersion2;
    if (size != expected || size > bytes.size()) {
        return std::unexpected(
            invalid("size", std::format("size 0x{:02x}, expected 0x{:02x}", size, expected)));
    }
    pib.size = static_cast<std::uint8_t>(size);
    pib.crc = bigEndian32(bytes, size - 4);
    const std::uint32_t computed = pibCrc32(bytes.first(size - 4));
    if (pib.crc != computed) {
        return std::unexpected(invalid(
            "crc", std::format("stored CRC 0x{:08x}, computed 0x{:08x}", pib.crc, computed)));
    }
    pib.vendorName = ascii(bytes, vendorName);
    pib.productName = ascii(bytes, productName);
    pib.hwVariant = ascii(bytes, hwVariant);
    pib.hwRevision = ascii(bytes, hwRevision);
    pib.serialNumber = ascii(bytes, serialNumber);
    pib.claimToken = ascii(bytes, claimToken);
    pib.blePasskey = ascii(bytes, blePasskey);
    return pib;
}

QJsonObject pibToJson(const Pib& pib) {
    const auto text = [](const std::string& value) {
        return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    };
    QJsonObject data;
    data.insert(QStringLiteral("signature"), text(std::format("0x{:08x}", pib.signature)));
    data.insert(QStringLiteral("version"), text(std::format("0x{:02x}", pib.version)));
    data.insert(QStringLiteral("size"), text(std::format("0x{:02x}", pib.size)));
    data.insert(QStringLiteral("crc"), text(std::format("0x{:08x}", pib.crc)));
    data.insert(QStringLiteral("vendor_name"), text(pib.vendorName));
    data.insert(QStringLiteral("product_name"), text(pib.productName));
    data.insert(QStringLiteral("hw_variant"), text(pib.hwVariant));
    data.insert(QStringLiteral("hw_revision"), text(pib.hwRevision));
    data.insert(QStringLiteral("serial_number"), text(pib.serialNumber));
    data.insert(QStringLiteral("claim_token"), text(pib.claimToken));
    data.insert(QStringLiteral("ble_passkey"), text(pib.blePasskey));
    return data;
}

} // namespace hio::device
