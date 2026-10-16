#pragma once

// Session operations (plan/features/0003-m2-flashing.md, ADR-0057, ADR-0062): flash, LTE bundle,
// erase, recover, modem update, reset and memory read run as one command on the session worker
// and report progress and one result.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/diag/check.h"
#include "hio/firmware/program_progress.h"
#include "hio/hw/jlink/programmer.h"

#include <QMetaType>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hio::device {

struct TargetProfile;

using firmware::ProgramPhase;
using jlink::ChipInfo;
using jlink::ResetMode;

// Unique per session (counts from 1).
using OperationId = std::uint64_t;

enum class OperationKind : std::uint8_t {
    Flash,
    EraseCode,
    EraseAll,
    Recover,
    Modem,
    LteBundle,
    Reset,
    ReadMemory,
    Diagnose, // M4 (plan/features/0007, ADR-0069)
};

// One step of a Diagnose operation (ADR-0069): "probe", "power", "connect", "identify", "rtt",
// "rtt-buffers". Reuses the check status of libs/diag (hio::diag_types).
struct DiagnoseStep {
    std::string id;
    diag::CheckStatus status = diag::CheckStatus::Pending;
    std::string result;              // short translated result text
    std::optional<hio::Error> error; // Warning / Failed only
};

// The step ids of Diagnose in order, and its bounds (ADR-0069, plan/features/0007 § Bounds).
inline constexpr std::array<std::string_view, 6> diagnoseStepIds{
    "probe", "power", "connect", "identify", "rtt", "rtt-buffers"};
inline constexpr std::size_t maxDiagnoseSteps = 16;
inline constexpr std::chrono::milliseconds diagnoseBudget{15000};
// The RTT search budget of the `rtt` step.
inline constexpr std::chrono::milliseconds diagnoseRttBudget{3000};

struct DiagnoseStepInfo {
    std::string id;
    std::string title; // translated, e.g. "Connection to the application MCU"
};

// The Diagnose steps of `target` with their translated titles (CLI, check lists).
[[nodiscard]] std::vector<DiagnoseStepInfo> diagnoseSteps(const TargetProfile& target);

struct OperationProgress {
    OperationKind kind = OperationKind::Flash;
    // Static per kind + target (+ cross-target), for steppers (plannedPhases()).
    std::span<const ProgramPhase> plannedPhases;
    ProgramPhase phase = ProgramPhase::Connecting;
    int step = 1; // LteBundle: 1/2 modem, 2/2 application
    int steps = 1;
    std::uint64_t done = 0;
    std::uint64_t total = 0; // 0 = indeterminate
    std::chrono::milliseconds elapsed{};
};

struct OperationResult {
    OperationKind kind = OperationKind::Flash;
    Status status; // error incl. code/wizard; cancel -> `cancelled`
    std::chrono::milliseconds duration{};
    std::optional<ChipInfo> chip; // the identified part
    std::vector<std::byte> data;  // ReadMemory only (<= maxReadMemoryBytes)
    // Diagnose only (<= maxDiagnoseSteps): every step in order. A Diagnose whose steps ran is a
    // success (`status` ok) even when a step failed; `status` is an error only when the operation
    // itself could not run (busy, cancelled, internal).
    std::vector<DiagnoseStep> checks;
};

struct FlashOptions {
    bool halt = false;      // leave the CPU halted after programming
    bool allowUicr = false; // ADR-0036: program the image's UICR records (the PIB is kept)
};

// Largest readMemory() (64 KiB).
inline constexpr std::size_t maxReadMemoryBytes = jlink::maxReadMemoryBytes;

// The phases an operation of `kind` goes through, in order. `crossTarget`: the operation's target
// differs from the session's console target (ADR-0057: SwitchingTarget first, Restoring last).
// `recoverImage`: a Recover that programs the nRF91 disable-AP-protect image (ADR-0039).
[[nodiscard]] std::span<const ProgramPhase> plannedPhases(OperationKind kind, bool crossTarget,
                                                          bool recoverImage = false) noexcept;

// Stable lowercase ids used in JSON: "flash", "eraseCode", "eraseAll", "recover", "modem",
// "lteBundle", "reset", "readMemory", "diagnose".
[[nodiscard]] std::string_view operationKindId(OperationKind kind) noexcept;

} // namespace hio::device

Q_DECLARE_METATYPE(hio::device::OperationProgress)
Q_DECLARE_METATYPE(hio::device::OperationResult)
Q_DECLARE_METATYPE(hio::device::DiagnoseStep)
