#pragma once

// The compiled-in device profiles (ADR-0032), defined one per file in src/profiles/ and listed in
// ProfileRegistry (profile.cpp). Constant-initialized: no static initialization order issues.

#include "hio/device/profile.h"

namespace hio::device::profiles {

extern const DeviceProfile chester;

} // namespace hio::device::profiles
