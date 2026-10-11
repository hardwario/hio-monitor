#pragma once

// Session operations (plan/features/0003-m2-flashing.md, ADR-0057, ADR-0062): flash, LTE bundle,
// erase, recover, modem update, reset and memory read run as one command on the session worker
// and report progress and one result.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/firmware/program_progress.h"
#include "hio/hw/jlink/programmer.h"

#include <QMetaType>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace hio::device {

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
};

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
// "lteBundle", "reset", "readMemory".
[[nodiscard]] std::string_view operationKindId(OperationKind kind) noexcept;

} // namespace hio::device

Q_DECLARE_METATYPE(hio::device::OperationProgress)
Q_DECLARE_METATYPE(hio::device::OperationResult)
