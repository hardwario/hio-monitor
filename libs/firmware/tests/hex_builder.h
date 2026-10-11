#pragma once

// Builds Intel HEX text for tests (records with correct checksums unless told otherwise).

#include "hio/firmware/firmware_image.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hio::firmware::test {

inline std::string hexRecord(std::uint8_t type, std::uint16_t address,
                             const std::vector<std::uint8_t>& data, int checksumDelta = 0) {
    auto sum = static_cast<std::uint8_t>(data.size());
    sum = static_cast<std::uint8_t>(sum + (address >> 8U) + (address & 0xFFU) + type);
    constexpr std::string_view digits = "0123456789ABCDEF";
    std::string line = std::format(":{:02X}{:04X}{:02X}", data.size(), address, type);
    line.reserve(line.size() + (2 * data.size()) + 3);
    for (const std::uint8_t byte : data) {
        line += digits[byte >> 4U];
        line += digits[byte & 0x0FU];
        sum = static_cast<std::uint8_t>(sum + byte);
    }
    const auto checksum = static_cast<std::uint8_t>((0x100 - sum + checksumDelta) & 0xFF);
    line += std::format("{:02X}\n", checksum);
    return line;
}

inline std::string eofRecord() { return ":00000001FF\n"; }

inline std::string linearAddress(std::uint16_t upper) {
    return hexRecord(
        0x04, 0,
        {static_cast<std::uint8_t>(upper >> 8U), static_cast<std::uint8_t>(upper & 0xFFU)});
}

// HEX text of `image` (up to 16-byte data records, extended linear addresses).
inline std::string toHex(const FirmwareImage& image) {
    std::string text;
    std::uint64_t upper = 0x1'0000'0000ULL; // none yet
    for (const Segment& segment : image.segments()) {
        std::size_t position = 0;
        while (position < segment.data.size()) {
            const std::uint64_t address = segment.address + position;
            if ((address >> 16U) != upper) {
                upper = address >> 16U;
                text += linearAddress(static_cast<std::uint16_t>(upper));
            }
            const auto count = std::min<std::uint64_t>(
                {16, segment.data.size() - position, 0x10000 - (address & 0xFFFFU)});
            std::vector<std::uint8_t> chunk;
            chunk.reserve(count);
            for (std::size_t j = 0; j < count; ++j) {
                chunk.push_back(static_cast<std::uint8_t>(segment.data[position + j]));
            }
            text += hexRecord(0x00, static_cast<std::uint16_t>(address & 0xFFFFU), chunk);
            position += count;
        }
    }
    return text + eofRecord();
}

inline std::vector<std::byte> bytesOf(const std::string& text) {
    const auto view = std::as_bytes(std::span(text.data(), text.size()));
    return {view.begin(), view.end()};
}

inline std::vector<std::byte> filled(std::size_t size, std::uint8_t value) {
    return std::vector<std::byte>(size, static_cast<std::byte>(value));
}

} // namespace hio::firmware::test
