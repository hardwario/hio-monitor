#pragma once

// Device profiles: compiled-in C++ data (ADR-0032). A profile describes a device and its targets
// (MCUs): J-Link device names, RTT buffer names, console rules and defaults. Profiles contain data
// only; behaviour differences are explicit typed fields (e.g. hashPrefixIsLog).
//
// Adding a device = one new file in src/profiles/ + one registration line in profile.cpp + tests.

#include <optional>
#include <span>
#include <string_view>

namespace hio::device {

struct TargetProfile {
    std::string_view id;          // "app", "lte"
    std::string_view jlinkDevice; // J-Link device name, e.g. "NRF52840_xxAA"
    std::string_view rttTerminal; // RTT shell buffer name ("" = no RTT console)
    std::string_view rttLogger;   // RTT log buffer name ("" = none)
    bool hashPrefixIsLog;         // shell lines starting with '#' are log lines (older CHESTER fw)
    // J-Link speed in kHz; nullopt = automatic (JLINKARM_SetSpeed(0), as pylink; ADR-0090).
    std::optional<int> defaultSpeedKhz;
    int defaultLatencyMs;
    int minTargetMillivolts; // below this VTref the target counts as unpowered
};

struct DeviceProfile {
    std::string_view id;          // "chester"
    std::string_view displayName; // product name, not translated ("CHESTER")
    std::span<const TargetProfile> targets;
};

class ProfileRegistry {
public:
    // nullptr when no profile has this id.
    [[nodiscard]] static const DeviceProfile* find(std::string_view id) noexcept;
    [[nodiscard]] static std::span<const DeviceProfile* const> all() noexcept;
};

// nullptr when the profile has no target with this id.
[[nodiscard]] const TargetProfile* findTarget(const DeviceProfile& profile,
                                              std::string_view id) noexcept;

} // namespace hio::device
