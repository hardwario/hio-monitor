#pragma once

// EventChannel<T> - a bounded event queue from the BLE thread to one owner-thread subscriber, with
// a coalesced wake-up (ADR-0080, ADR-0031 pattern; plan/features/0011).
//
// The producer (BLE thread) push()es and the channel calls the subscriber's wake function, which
// must be cheap and thread-safe (typically a QMetaObject::invokeMethod(..., Qt::QueuedConnection)
// guarded by an atomic flag). detach() clears the wake function under the same mutex, so after it
// returns the producer never calls it again - the subscriber may be destroyed then, while the
// producer still holds the shared channel.
//
// Bound: the queue's capacity; when full, DropOldestMatching (the `droppable` predicate: data,
// never state events), counted in droppedCount(). Thread safety: all members, any thread.

#include "hio/core/bounded_queue.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

namespace hio::ble {

template <class T> class EventChannel {
public:
    EventChannel(std::size_t capacity, std::function<bool(const T&)> droppable,
                 std::function<void()> wake)
        : m_queue(capacity, std::move(droppable)), m_wake(std::move(wake)) {}

    // Queues `event` and wakes the subscriber. False when the channel was closed.
    bool push(T event) {
        if (!m_queue.push(std::move(event))) {
            return false;
        }
        const std::scoped_lock lock(m_wakeMutex);
        if (m_wake) {
            m_wake();
        }
        return true;
    }

    // Subscriber side: the next event, without waiting.
    [[nodiscard]] std::optional<T> pop() { return m_queue.pop({}, std::chrono::milliseconds(0)); }
    // Waits up to `timeout` (a thread that is not the owner's event loop, e.g. a doctor check).
    [[nodiscard]] std::optional<T> wait(std::chrono::milliseconds timeout) {
        return m_queue.pop({}, timeout);
    }
    [[nodiscard]] std::size_t size() const { return m_queue.size(); }
    [[nodiscard]] std::uint64_t droppedCount() const { return m_queue.droppedCount(); }

    // No wake-up after this returns; later pushes fail.
    void detach() {
        m_queue.close();
        const std::scoped_lock lock(m_wakeMutex);
        m_wake = nullptr;
    }

private:
    BoundedQueue<T> m_queue;
    std::mutex m_wakeMutex;
    std::function<void()> m_wake; // guarded by m_wakeMutex
};

} // namespace hio::ble
