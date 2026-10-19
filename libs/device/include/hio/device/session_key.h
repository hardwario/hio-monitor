#pragma once

// SessionKey - what a session of this process is bound to (ADR-0083, plan 0011 B2): a J-Link
// probe by serial number or a Bluetooth device by its id. One session per key; J-Link and BLE
// sessions share SessionRegistry::maxSessions.

#include "hio/hw/ble/types.h"

#include <cstdint>
#include <variant>

namespace hio::device {

struct ProbeKey {
    std::uint32_t sn = 0;
    friend bool operator==(const ProbeKey&, const ProbeKey&) = default;
};

struct BleKey {
    ble::BleDeviceId id;
    friend bool operator==(const BleKey&, const BleKey&) = default;
};

using SessionKey = std::variant<ProbeKey, BleKey>;

} // namespace hio::device
