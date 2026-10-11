#include "hio/device/profile.h"

#include "profiles/profiles.h"

#include <algorithm>
#include <array>
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

} // namespace hio::device
