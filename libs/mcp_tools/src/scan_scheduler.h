#pragma once

// ScanScheduler - spreads the regex scans of wait_for_state over owner-thread turns (ADR-0097,
// plan/features/0013 "Owner-thread stalls": with 32 calls and 10 000 lines/s no owner-thread task
// may take more than 20 ms).
//
// A scanner asks for a turn when new lines arrived (request()). One turn runs the queued scanners
// round-robin, `chunkLines` lines each, until `turnBudget` is used up, then posts the next turn
// (QTimer::singleShot(0)) so that other events (timers, sockets, the GUI) run in between.
//
// Thread safety: owner thread only.

#include <QObject>

#include <chrono>
#include <cstddef>
#include <deque>

namespace hio::mcp::tools::detail {

class LineScanner {
public:
    LineScanner() = default;
    LineScanner(const LineScanner&) = delete;
    LineScanner& operator=(const LineScanner&) = delete;
    LineScanner(LineScanner&&) = delete;
    LineScanner& operator=(LineScanner&&) = delete;
    virtual ~LineScanner() = default;

    // Scans at most `maxLines` lines; true when more lines are waiting.
    virtual bool scanSome(std::size_t maxLines) = 0;
};

class ScanScheduler final : public QObject {
    Q_OBJECT

public:
    static constexpr std::chrono::milliseconds turnBudget{4};
    static constexpr std::size_t chunkLines = 64;

    using QObject::QObject;

    // Queues `scanner` (once) and schedules a turn.
    void request(LineScanner& scanner);
    // Must be called before the scanner is destroyed (idempotent).
    void remove(LineScanner& scanner);
    [[nodiscard]] std::size_t queued() const noexcept { return m_queue.size(); }

private:
    void schedule();
    void turn();

    std::deque<LineScanner*> m_queue; // non-owning; each scanner removes itself
    LineScanner* m_current = nullptr; // the scanner inside scanSome()
    bool m_currentRemoved = false;
    bool m_scheduled = false;
};

} // namespace hio::mcp::tools::detail
