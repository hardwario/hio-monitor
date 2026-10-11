#pragma once

// Turns the phase reports of a running operation into OperationProgress events
// (plan/features/0003: at most one event per interval per operation, plus every phase or step
// change and the final state). Phases only move forward through the operation's plannedPhases;
// a report of an earlier or unplanned phase updates the counters of the current phase.
//
// Thread safety: none (the session worker).

#include "hio/device/operation.h"
#include "hio/firmware/program_progress.h"
#include "hio/hw/jlink/clock.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>

namespace hio::device::detail {

class OperationReporter {
public:
    using EmitFn = std::function<void(const OperationProgress&)>;

    OperationReporter(OperationKind kind, std::span<const ProgramPhase> planned,
                      jlink::Clock& clock, std::chrono::milliseconds interval, EmitFn emitFn);

    // A phase start (indeterminate) or a progress report.
    void phase(ProgramPhase phase);
    void update(const firmware::ProgramProgress& progress);
    // LteBundle: 1/2 modem, 2/2 application. The next report is emitted at once.
    void step(int step, int steps);
    // Emits the last coalesced update, if any.
    void flush();

    [[nodiscard]] std::chrono::milliseconds elapsed() const;
    [[nodiscard]] std::span<const ProgramPhase> planned() const noexcept { return m_planned; }

private:
    void emitNow();

    std::span<const ProgramPhase> m_planned;
    jlink::Clock* m_clock;
    std::chrono::milliseconds m_interval;
    EmitFn m_emit;
    jlink::Clock::TimePoint m_start;
    jlink::Clock::TimePoint m_lastEmit;
    std::size_t m_index = 0; // current position in m_planned
    bool m_started = false;  // something was emitted
    bool m_pending = false;  // a coalesced update waits for flush()
    bool m_stepChanged = false;
    OperationProgress m_current;
};

} // namespace hio::device::detail
