#include "operation_reporter.h"

#include "hio/device/operation.h"
#include "hio/firmware/program_progress.h"
#include "hio/hw/jlink/clock.h"

#include <chrono>
#include <cstddef>
#include <span>
#include <utility>

namespace hio::device::detail {

OperationReporter::OperationReporter(OperationKind kind, std::span<const ProgramPhase> planned,
                                     jlink::Clock& clock, std::chrono::milliseconds interval,
                                     EmitFn emitFn)
    : m_planned(planned), m_clock(&clock), m_interval(interval), m_emit(std::move(emitFn)),
      m_start(clock.now()), m_lastEmit(m_start) {
    m_current.kind = kind;
    m_current.plannedPhases = planned;
    m_current.phase = planned.empty() ? ProgramPhase::Connecting : planned.front();
}

std::chrono::milliseconds OperationReporter::elapsed() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(m_clock->now() - m_start);
}

void OperationReporter::phase(ProgramPhase phase) {
    update(firmware::ProgramProgress{.phase = phase, .done = 0, .total = 0});
}

void OperationReporter::update(const firmware::ProgramProgress& progress) {
    // Forward only: the first occurrence of the phase at or after the current position.
    std::size_t index = m_index;
    for (std::size_t i = m_index; i < m_planned.size(); ++i) {
        if (m_planned[i] == progress.phase) {
            index = i;
            break;
        }
    }
    const bool changed = !m_started || m_stepChanged || index != m_index;
    if (changed && m_pending) {
        emitNow(); // the last state of the previous phase (e.g. 100 %) is not lost
    }
    m_stepChanged = false;
    m_index = index;
    if (!m_planned.empty()) {
        m_current.phase = m_planned[m_index];
    }
    if (m_current.phase == progress.phase) {
        m_current.done = progress.done;
        m_current.total = progress.total;
    } else if (changed) {
        m_current.done = 0;
        m_current.total = 0;
    }
    if (changed || m_clock->now() - m_lastEmit >= m_interval) {
        emitNow();
    } else {
        m_pending = true;
    }
}

void OperationReporter::step(int step, int steps) {
    if (m_pending) {
        emitNow(); // the last state of the previous step
    }
    m_current.step = step;
    m_current.steps = steps;
    m_stepChanged = true; // the next report is emitted at once
}

void OperationReporter::flush() {
    if (m_pending) {
        emitNow();
    }
}

void OperationReporter::emitNow() {
    m_started = true;
    m_pending = false;
    m_lastEmit = m_clock->now();
    m_current.elapsed = elapsed();
    if (m_emit) {
        m_emit(m_current);
    }
}

} // namespace hio::device::detail
