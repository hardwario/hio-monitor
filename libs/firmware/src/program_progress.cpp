#include "hio/firmware/program_progress.h"

#include <string_view>

namespace hio::firmware {

std::string_view phaseId(ProgramPhase phase) noexcept {
    switch (phase) {
    case ProgramPhase::Connecting:
        return "connecting";
    case ProgramPhase::SwitchingTarget:
        return "switchingTarget";
    case ProgramPhase::Erasing:
        return "erasing";
    case ProgramPhase::Programming:
        return "programming";
    case ProgramPhase::Verifying:
        return "verifying";
    case ProgramPhase::Modem:
        return "modem";
    case ProgramPhase::Recovering:
        return "recovering";
    case ProgramPhase::Resetting:
        return "resetting";
    case ProgramPhase::Restoring:
        return "restoring";
    }
    return "connecting";
}

} // namespace hio::firmware
