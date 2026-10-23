#include "hio/device/operation.h"

#include "hio/device/profile.h"

#include <QCoreApplication>
#include <QString>

#include <array>
#include <span>
#include <string_view>
#include <vector>

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
constexpr std::array diagnosePhases{P::Connecting};

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
constexpr std::array diagnoseCross{P::SwitchingTarget, P::Connecting, P::Restoring};

// The cross-target or the same-target phases.
constexpr std::span<const P> pick(bool crossTarget, std::span<const P> cross,
                                  std::span<const P> same) noexcept {
    return crossTarget ? cross : same;
}

} // namespace

std::span<const ProgramPhase> plannedPhases(OperationKind kind, bool crossTarget,
                                            bool recoverImage) noexcept {
    switch (kind) {
    case OperationKind::Flash:
        return pick(crossTarget, flashCross, flashPhases);
    case OperationKind::EraseCode:
    case OperationKind::EraseAll:
        return pick(crossTarget, eraseCross, erasePhases);
    case OperationKind::Recover:
        if (recoverImage) {
            return pick(crossTarget, recoverImageCross, recoverImagePhases);
        }
        return pick(crossTarget, recoverCross, recoverPhases);
    case OperationKind::Modem:
        return pick(crossTarget, modemCross, modemPhases);
    case OperationKind::LteBundle:
        return pick(crossTarget, bundleCross, bundlePhases);
    case OperationKind::Reset:
        return pick(crossTarget, resetCross, resetPhases);
    case OperationKind::ReadMemory:
        return pick(crossTarget, readCross, readPhases);
    case OperationKind::Diagnose:
        return pick(crossTarget, diagnoseCross, diagnosePhases);
    case OperationKind::Halt:
    case OperationKind::Go:
    case OperationKind::TargetStatus:
    case OperationKind::WriteMemory:
    case OperationKind::ReadRegisters:
    case OperationKind::MemoryZones:
        // M6: like a memory read (the work is one short step after Connecting).
        return pick(crossTarget, readCross, readPhases);
    case OperationKind::WriteFlash:
        return pick(crossTarget, flashCross, flashPhases);
    }
    return {};
}

bool isLightOperation(OperationKind kind) noexcept {
    switch (kind) {
    case OperationKind::Halt:
    case OperationKind::Go:
    case OperationKind::TargetStatus:
    case OperationKind::ReadMemory:
    case OperationKind::WriteMemory:
    case OperationKind::ReadRegisters:
    case OperationKind::MemoryZones:
        return true;
    default:
        return false;
    }
}

std::vector<DiagnoseStepInfo> diagnoseSteps(const TargetProfile& target) {
    return {
        {.id = "probe",
         .title = QCoreApplication::translate("hio::device", "J-Link probe").toStdString()},
        {.id = "power",
         .title = QCoreApplication::translate("hio::device", "Device power").toStdString()},
        {.id = "connect",
         .title = QCoreApplication::translate("hio::device", "Connection to the %1 MCU")
                      .arg(targetDisplayName(target))
                      .toStdString()},
        {.id = "identify",
         .title = QCoreApplication::translate("hio::device", "MCU type").toStdString()},
        {.id = "rtt",
         .title = QCoreApplication::translate("hio::device", "Device console (RTT)").toStdString()},
        {.id = "rtt-buffers",
         .title = QCoreApplication::translate("hio::device", "Console channels").toStdString()},
    };
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
    case OperationKind::Diagnose:
        return "diagnose";
    case OperationKind::Halt:
        return "halt";
    case OperationKind::Go:
        return "go";
    case OperationKind::TargetStatus:
        return "targetStatus";
    case OperationKind::WriteMemory:
        return "writeMemory";
    case OperationKind::ReadRegisters:
        return "readRegisters";
    case OperationKind::MemoryZones:
        return "memoryZones";
    case OperationKind::WriteFlash:
        return "writeFlash";
    }
    return "flash";
}

} // namespace hio::device
