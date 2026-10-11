// HARDWARIO CHESTER (plan/features/0002-m1-jlink-rtt.md; py-hardwario
// hardwario/chester/cli/app.py).

#include "hio/device/profile.h"
#include "profiles.h"

#include <array>

namespace hio::device::profiles {

namespace {

constexpr std::array chesterTargets{
    // Application MCU: the RTT console of `hio chester app console` / `command`.
    TargetProfile{.id = "app",
                  .jlinkDevice = "NRF52840_xxAA",
                  .rttTerminal = "Terminal",
                  .rttLogger = "Logger",
                  .hashPrefixIsLog = true,
                  .defaultSpeedKhz = 2000,
                  .defaultLatencyMs = 50,
                  .minTargetMillivolts = 1000},
    // LTE modem MCU (nRF91 series, J-Link device name of the nRF9151; py-hardwario maps every
    // nRF91 to it). Declared for M2 (flashing); no RTT console.
    TargetProfile{.id = "lte",
                  .jlinkDevice = "NRF9151_XXCA",
                  .rttTerminal = "",
                  .rttLogger = "",
                  .hashPrefixIsLog = false,
                  .defaultSpeedKhz = 2000,
                  .defaultLatencyMs = 50,
                  .minTargetMillivolts = 1000},
};

} // namespace

constinit const DeviceProfile chester{
    .id = "chester", .displayName = "CHESTER", .targets = chesterTargets};

} // namespace hio::device::profiles
