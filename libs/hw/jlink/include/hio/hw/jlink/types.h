#pragma once

// Plain data types of the J-Link adapter (plan/features/0002-m1-jlink-rtt.md).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hio::jlink {

// Minimum supported J-Link Software (ADR-0029), in the library's version format
// major * 10000 + minor * 100 + revision (8.10a = 81001).
inline constexpr std::uint32_t minimumLibraryVersion = 80000;

enum class Connection : std::uint8_t {
    Usb,
    Ip,
};

struct ProbeInfo {
    std::uint32_t serialNumber = 0;
    std::string product; // e.g. "Test Probe" (may be empty when the library does not say)
    Connection connection = Connection::Usb;
    std::string firmware; // firmware identification string (may be empty until the probe is open)
};

// The probe's firmware compared with the one bundled in the library.
enum class FirmwareStatus : std::uint8_t {
    Current,
    Outdated, // older than the library's firmware
    Newer,    // newer than (or different from) the library's firmware
    Unknown,  // the firmware strings cannot be read or compared (review R11, 2026-10-10)
};

struct HardwareStatus {
    int targetMillivolts = 0; // VTref measured by the probe
};

enum class RttDirection : std::uint8_t {
    Up,   // target -> host
    Down, // host -> target
};

struct RttBufferInfo {
    std::string name; // UTF-8; invalid sequences replaced
    std::uint32_t size = 0;
    std::uint32_t flags = 0;
};

// One J-Link memory zone of the connected target (JLINK_GetMemZones, plan/features/0013).
struct MemoryZone {
    std::string name;        // UTF-8, e.g. "AHB-AP"; invalid sequences replaced
    std::string description; // UTF-8
    std::uint64_t virtualAddress = 0;
};

// One core register (JLINKARM_GetRegisterList / GetRegisterName / ReadRegs, plan/features/0013).
struct RegisterValue {
    std::string name; // e.g. "R0", "PC"
    std::uint32_t value = 0;
};

// "8.10a" for 81001, "7.94k" for 79411 (pylink's format).
[[nodiscard]] std::string formatLibraryVersion(std::uint32_t version);

// Stable lowercase ids used in JSON ("usb", "ip"; "current", "outdated", "newer", "unknown").
[[nodiscard]] std::string_view connectionId(Connection connection) noexcept;
[[nodiscard]] std::string_view firmwareStatusId(FirmwareStatus status) noexcept;

// Compares two firmware identification strings ("J-Link V11 compiled Sep 21 2023 10:38:43") the
// way pylink's firmware_outdated()/firmware_newer() do. nullopt when a date cannot be parsed.
[[nodiscard]] std::optional<FirmwareStatus> compareFirmware(std::string_view probeFirmware,
                                                            std::string_view libraryFirmware);

// The part of a firmware string before "compiled" (the id passed to GetEmbeddedFWString).
[[nodiscard]] std::string firmwareId(std::string_view firmware);

} // namespace hio::jlink
