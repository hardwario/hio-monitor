#include "hio/device/operation.h"

#include <array>
#include <span>
#include <string_view>

namespace hio::device {

namespace {

using P = ProgramPhase;

// Same target as the console (or a session opened on the operation's target).
constexpr std::array flashPhases{P::Connecting, P::Erasing, P::Programming, P::Verifying,
                                 P::Resetting};
constexpr std::array erasePhases{P::Connecting, P::Erasing};
constexpr std::array recoverPhases{P::Connecting, P::Recovering, P::Resetting};
constexpr std::array recoverImagePhases{P::Connecting,  P::Recovering, P::Erasing,
                                        P::Programming, P::Verifying,  P::Resetting};
constexpr std::array modemPhases{P::Modem};
constexpr std::array bundlePhases{P::Modem,       P::Connecting, P::Erasing,
                                  P::Programming, P::Verifying,  P::Resetting};
constexpr std::array resetPhases{P::Connecting, P::Resetting};
constexpr std::array readPhases{P::Connecting};

// Cross-target (ADR-0057): SwitchingTarget first, Restoring last.
constexpr std::array flashCross{P::SwitchingTarget, P::Connecting, P::Erasing,  P::Programming,
                                P::Verifying,       P::Resetting,  P::Restoring};
constexpr std::array eraseCross{P::SwitchingTarget, P::Connecting, P::Erasing, P::Restoring};
constexpr std::array recoverCross{P::SwitchingTarget, P::Connecting, P::Recovering, P::Resetting,
                                  P::Restoring};
constexpr std::array recoverImageCross{P::SwitchingTarget, P::Connecting,  P::Recovering,
                                       P::Erasing,         P::Programming, P::Verifying,
                                       P::Resetting,       P::Restoring};
constexpr std::array modemCross{P::SwitchingTarget, P::Modem, P::Restoring};
constexpr std::array bundleCross{P::SwitchingTarget, P::Modem,     P::Connecting, P::Erasing,
                                 P::Programming,     P::Verifying, P::Resetting,  P::Restoring};
constexpr std::array resetCross{P::SwitchingTarget, P::Connecting, P::Resetting, P::Restoring};
constexpr std::array readCross{P::SwitchingTarget, P::Connecting, P::Restoring};

} // namespace

std::span<const ProgramPhase> plannedPhases(OperationKind kind, bool crossTarget,
                                            bool recoverImage) noexcept {
    switch (kind) {
    case OperationKind::Flash:
        return crossTarget ? std::span<const P>(flashCross) : std::span<const P>(flashPhases);
    case OperationKind::EraseCode:
    case OperationKind::EraseAll:
        return crossTarget ? std::span<const P>(eraseCross) : std::span<const P>(erasePhases);
    case OperationKind::Recover:
        if (recoverImage) {
            return crossTarget ? std::span<const P>(recoverImageCross)
                               : std::span<const P>(recoverImagePhases);
        }
        return crossTarget ? std::span<const P>(recoverCross) : std::span<const P>(recoverPhases);
    case OperationKind::Modem:
        return crossTarget ? std::span<const P>(modemCross) : std::span<const P>(modemPhases);
    case OperationKind::LteBundle:
        return crossTarget ? std::span<const P>(bundleCross) : std::span<const P>(bundlePhases);
    case OperationKind::Reset:
        return crossTarget ? std::span<const P>(resetCross) : std::span<const P>(resetPhases);
    case OperationKind::ReadMemory:
        return crossTarget ? std::span<const P>(readCross) : std::span<const P>(readPhases);
    }
    return {};
}

std::string_view operationKindId(OperationKind kind) noexcept {
    switch (kind) {
    case OperationKind::Flash:
        return "flash";
    case OperationKind::EraseCode:
        return "eraseCode";
    case OperationKind::EraseAll:
        return "eraseAll";
    case OperationKind::Recover:
        return "recover";
    case OperationKind::Modem:
        return "modem";
    case OperationKind::LteBundle:
        return "lteBundle";
    case OperationKind::Reset:
        return "reset";
    case OperationKind::ReadMemory:
        return "readMemory";
    }
    return "flash";
}

} // namespace hio::device
