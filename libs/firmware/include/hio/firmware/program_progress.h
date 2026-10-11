#pragma once

// Progress of a programming step (plan/features/0003-m2-flashing.md, ADR-0057, ADR-0062).
// Shared by the J-Link programmer (libs/hw/jlink), the nRF Util modem updater (libs/hw/nrfutil)
// and the session operations (libs/device).

#include <cstdint>
#include <functional>
#include <string_view>

namespace hio::firmware {

enum class ProgramPhase : std::uint8_t {
    Connecting,
    SwitchingTarget, // ADR-0057: the probe is moved to the operation's target
    Erasing,
    Programming,
    Verifying,
    Modem, // nRF Util modem firmware update
    Recovering,
    Resetting,
    Restoring, // ADR-0057: the probe is moved back to the console target
};

struct ProgramProgress {
    ProgramPhase phase = ProgramPhase::Connecting;
    std::uint64_t done = 0;
    std::uint64_t total = 0; // 0 = indeterminate
};

// Called on the thread that runs the step (the session worker); must not throw.
using ProgressFn = std::function<void(const ProgramProgress&)>;

// Stable lowercase ids used in JSON: "connecting", "switchingTarget", "erasing", ...
[[nodiscard]] std::string_view phaseId(ProgramPhase phase) noexcept;

} // namespace hio::firmware
