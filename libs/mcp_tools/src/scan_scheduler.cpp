#include "scan_scheduler.h"

#include <QElapsedTimer>
#include <QTimer>

#include <algorithm>
#include <chrono>

namespace hio::mcp::tools::detail {

void ScanScheduler::request(LineScanner& scanner) {
    if (std::ranges::find(m_queue, &scanner) == m_queue.end()) {
        m_queue.push_back(&scanner);
    }
    schedule();
}

void ScanScheduler::remove(LineScanner& scanner) {
    std::erase(m_queue, &scanner);
    if (m_current == &scanner) {
        m_currentRemoved = true;
    }
}

void ScanScheduler::schedule() {
    if (m_scheduled || m_queue.empty()) {
        return;
    }
    m_scheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_scheduled = false;
        turn();
    });
}

void ScanScheduler::turn() {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!m_queue.empty() && std::chrono::milliseconds(elapsed.elapsed()) < turnBudget) {
        LineScanner* scanner = m_queue.front();
        m_queue.pop_front();
        // A scanner may finish (and remove itself) inside scanSome(); it is re-queued only when it
        // reports more work and did not remove itself meanwhile.
        m_current = scanner;
        m_currentRemoved = false;
        const bool more = scanner->scanSome(chunkLines);
        m_current = nullptr;
        if (more && !m_currentRemoved && std::ranges::find(m_queue, scanner) == m_queue.end()) {
            m_queue.push_back(scanner);
        }
    }
    schedule();
}

} // namespace hio::mcp::tools::detail
