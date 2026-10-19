#pragma once

// Types of the BLE transport (plan/features/0011-m5-ble-backend-cli.md, ADR-0083, ADR-0129).
// libs/hw/ble knows nothing about device profiles or console lines: it scans, connects to the
// Nordic UART Service (NUS) and moves bytes.

#include "hio/core/error.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hio::ble {

// "F1:22:33:44:55:66" (Linux / Windows: the Bluetooth address) or the CoreBluetooth UUID (macOS),
// ADR-0083.
using BleDeviceId = std::string;

enum class AdapterState : std::uint8_t {
    Ready,
    NotFound,
    PoweredOff,
    PermissionDenied,
    PermissionUndetermined, // macOS: not asked yet; the first scan asks (ADR-0086, ADR-0088)
    StackUnavailable,       // Linux: an adapter exists but BlueZ is not reachable (Q7)
};

struct AdapterInfo {
    AdapterState state = AdapterState::NotFound;
    std::string name;    // "" when unknown
    std::string address; // "" when unknown (always on macOS)
};

// One advertisement (or scan response) of a device.
struct Advertisement {
    BleDeviceId id;
    std::string name;                      // the advertised local name ("" none)
    int rssi = 0;                          // dBm
    std::vector<std::string> serviceUuids; // lowercase, 128-bit form
    std::chrono::steady_clock::time_point seen;
};

enum class WriteMode : std::uint8_t { WithResponse, WithoutResponse };

// The link is usable: TX notifications are enabled.
struct LinkReady {
    std::uint16_t mtu = 23; // ATT MTU; payload per write = mtu - 3
    WriteMode writeMode = WriteMode::WithResponse;
};

// An IBleApi link (one connection attempt chain to one device).
using LinkId = std::uint32_t;

// The service and characteristics of the console (from the device profile, ADR-0089).
struct NusUuids {
    std::string service; // lowercase, 128-bit form
    std::string rx;      // written by us
    std::string tx;      // notified by the device
};

// The standard Nordic UART Service; shown as `nusAdvertised` in scan rows (information only,
// ADR-0132).
inline constexpr std::string_view nusServiceUuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";

// ATT_MTU default and the ATT write header (ADR-0082).
inline constexpr std::uint16_t defaultMtu = 23;
inline constexpr std::uint16_t attWriteHeader = 3;

// The error of an adapter that cannot be used now (nullopt: Ready, or PermissionUndetermined -
// using it triggers the system question).
[[nodiscard]] std::optional<Error> adapterError(const AdapterInfo& adapter);

// Lowercase ASCII (UUID comparison).
[[nodiscard]] std::string toLowerAscii(std::string_view text);

// The canonical form of a device id (review B6), so that one device has one lock file, registry
// key and console file name: a Bluetooth address in upper case ("F1:22:33:44:55:66", via
// QBluetoothAddress), a UUID (macOS) in QUuid's form without braces ("6a0b3f1c-..."); anything else
// trimmed and unchanged. Scan rows use the same form (QtBleApi).
[[nodiscard]] BleDeviceId canonicalDeviceId(std::string_view id);
// Device ids compared without regard to ASCII case.
[[nodiscard]] bool sameDeviceId(std::string_view a, std::string_view b);

} // namespace hio::ble
