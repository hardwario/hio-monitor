#pragma once

// FirmwareIdentity - the firmware a device reported (plan/features/0028, ADR-0200): the data only.
// It lives in libs/core because it travels from the owner thread through the libs/hw/ble command
// queue to the console handler on the BLE thread (ble::SetFirmwareIdentity), and libs/hw/ble must
// not depend on libs/device. The rules that build and use it are in
// hwio/device/firmware_identity.h.

#include <cstddef>
#include <string>

namespace hwio {

struct FirmwareIdentity {
    // Bounds (chester-sdk buffer sizes): name <= maxNameBytes, version <= maxVersionBytes,
    // deviceSerial <= maxSerialBytes bytes.
    static constexpr std::size_t maxNameBytes = 64;
    static constexpr std::size_t maxVersionBytes = 48;
    static constexpr std::size_t maxSerialBytes = 32;

    std::string name;         // e.g. "CHESTER Clime"
    std::string version;      // e.g. "v3.0.0" or "v3.0.0-5-gabc1234"
    std::string deviceSerial; // "serial number" of `info show`; "" when unknown (never in names)
    friend bool operator==(const FirmwareIdentity&, const FirmwareIdentity&) = default;
};

} // namespace hwio
