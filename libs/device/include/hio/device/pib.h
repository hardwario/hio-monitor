#pragma once

// HARDWARIO Product Information Block (PIB) of a CHESTER (plan/features/0003, C3;
// plan/0002-legacy-inventory.md; py-hardwario hardwario/common/pib.py with nrf=True).
//
// 128 bytes at UICR + pibOffset, version 2, big-endian:
//   0x00 signature 0xBABECAFE   0x04 version 2   0x05 size (0x7B)
//   0x06 vendor name 17s        0x17 product name 17s   0x28 hardware variant 11s
//   0x33 hardware revision 7s   0x3A serial number 11s  0x45 claim token 33s
//   0x66 BLE passkey 17s        size-4 CRC-32 (reflected 0xEDB88320, init and xorout
//                                0xFFFFFFFF) over [0, size-4), stored big-endian
// Strings are ASCII with trailing NULs stripped (other bytes become '?').
// Checks in py-hardwario's order: version (1 or 2), signature, size, CRC. As in py-hardwario a
// CHESTER PIB of version 1 cannot pass the size check (its expected size exceeds 128 bytes).

#include "hio/core/result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace hio::device {

struct Pib {
    std::uint32_t signature = 0;
    std::uint8_t version = 0;
    std::uint8_t size = 0;
    std::uint32_t crc = 0;
    std::string vendorName;
    std::string productName;
    std::string hwVariant;
    std::string hwRevision;
    std::string serialNumber;
    std::string claimToken;
    std::string blePasskey;
};

// Errors: pib.invalid with the failed check ("version", "signature", "size", "crc") in the
// message and the values in `detail`.
[[nodiscard]] Result<Pib> decodePib(std::span<const std::byte, 128> bytes);

// CRC-32 as used by the PIB (exposed for tests).
[[nodiscard]] std::uint32_t pibCrc32(std::span<const std::byte> bytes) noexcept;

} // namespace hio::device
