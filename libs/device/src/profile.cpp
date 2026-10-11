#include "hio/device/profile.h"

#include "hio/firmware/firmware_image.h"
#include "profiles/profiles.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace hio::device {

namespace {

// Registration: one line per device profile (ADR-0032).
constexpr std::array<const DeviceProfile*, 1> registeredProfiles{
    &profiles::chester,
};

} // namespace

const DeviceProfile* ProfileRegistry::find(std::string_view id) noexcept {
    const auto* const it = std::ranges::find_if(
        registeredProfiles, [id](const DeviceProfile* profile) { return profile->id == id; });
    return it == registeredProfiles.end() ? nullptr : *it;
}

std::span<const DeviceProfile* const> ProfileRegistry::all() noexcept { return registeredProfiles; }

const TargetProfile* findTarget(const DeviceProfile& profile, std::string_view id) noexcept {
    const auto it = std::ranges::find(profile.targets, id, &TargetProfile::id);
    return it == profile.targets.end() ? nullptr : &*it;
}

const ChipPart* findPart(const TargetProfile& target, std::uint32_t part) noexcept {
    const auto it = std::ranges::find(target.parts, part, &ChipPart::part);
    return it == target.parts.end() ? nullptr : &*it;
}

firmware::AddressRange pibRange(const TargetProfile& target) noexcept {
    const std::uint64_t start = target.uicr.start + target.pibOffset;
    return firmware::AddressRange{.start = start, .end = start + pibSize};
}

} // namespace hio::device
