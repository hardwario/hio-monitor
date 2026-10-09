#pragma once

// Thread-safe bounded FIFO queue for cross-thread communication (CLAUDE.md: every queue bounded).
//
// Bound: at most `capacity` elements. What happens when full is chosen per queue:
//   Block      push() waits for space; a stop request or close() ends the wait -> `cancelled`
//   DropOldest push() always succeeds; the oldest element is discarded and counted
//   Reject     push() fails immediately with `internal.queue_full`
// Thread safety: all members may be called from any thread (one mutex + condition variables).

#include "hio/core/error.h"
#include "hio/core/result.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

namespace hio {

enum class OverflowPolicy : std::uint8_t {
    Block,
    DropOldest,
    Reject,
};

template <class T> class BoundedQueue {
public:
    // Contract: capacity >= 1 (a zero capacity terminates the process: it is a programming error).
    BoundedQueue(std::size_t capacity, OverflowPolicy policy)
        : m_capacity(checkedCapacity(capacity)), m_policy(policy) {}

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;
    BoundedQueue(BoundedQueue&&) = delete;
    BoundedQueue& operator=(BoundedQueue&&) = delete;
    ~BoundedQueue() = default;

    // Errors: `cancelled` when the queue is closed or (Block only) stop is requested while
    // waiting; `internal.queue_full` when full with the Reject policy.
    [[nodiscard]] Status push(T value, std::stop_token stop = {}) {
        std::unique_lock lock(m_mutex);
        if (m_closed) {
            return fail(ErrorCode::Cancelled);
        }
        if (m_items.size() >= m_capacity) {
            switch (m_policy) {
            case OverflowPolicy::Reject:
                return fail(ErrorCode::InternalQueueFull);
            case OverflowPolicy::DropOldest:
                m_items.pop_front();
                ++m_dropped;
                break;
            case OverflowPolicy::Block:
                if (!m_notFull.wait(lock, std::move(stop),
                                    [this] { return m_closed || m_items.size() < m_capacity; })) {
                    return fail(ErrorCode::Cancelled);
                }
                if (m_closed) {
                    return fail(ErrorCode::Cancelled);
                }
                break;
            }
        }
        m_items.push_back(std::move(value));
        lock.unlock();
        m_notEmpty.notify_one();
        return {};
    }

    // Waits up to `timeout` for an element. Returns nullopt on timeout, on a stop request, or
    // when the queue is closed and empty (elements pushed before close() are still delivered).
    [[nodiscard]] std::optional<T> pop(std::stop_token stop, std::chrono::milliseconds timeout) {
        std::unique_lock lock(m_mutex);
        if (!m_notEmpty.wait_for(lock, std::move(stop), timeout,
                                 [this] { return m_closed || !m_items.empty(); })) {
            return std::nullopt;
        }
        if (m_items.empty()) {
            return std::nullopt; // closed
        }
        std::optional<T> value(std::move(m_items.front()));
        m_items.pop_front();
        lock.unlock();
        m_notFull.notify_one();
        return value;
    }

    // Wakes all waiters; later push() calls fail with `cancelled`.
    void close() {
        {
            const std::scoped_lock lock(m_mutex);
            m_closed = true;
        }
        m_notEmpty.notify_all();
        m_notFull.notify_all();
    }

    [[nodiscard]] bool isClosed() const {
        const std::scoped_lock lock(m_mutex);
        return m_closed;
    }

    [[nodiscard]] std::size_t size() const {
        const std::scoped_lock lock(m_mutex);
        return m_items.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }

    // Number of elements discarded by the DropOldest policy.
    [[nodiscard]] std::uint64_t droppedCount() const {
        const std::scoped_lock lock(m_mutex);
        return m_dropped;
    }

private:
    static std::size_t checkedCapacity(std::size_t capacity) {
        if (capacity == 0) {
            std::abort(); // contract violation: BoundedQueue capacity must be >= 1
        }
        return capacity;
    }

    const std::size_t m_capacity;
    const OverflowPolicy m_policy;
    mutable std::mutex m_mutex;
    std::condition_variable_any m_notEmpty;
    std::condition_variable_any m_notFull;
    std::deque<T> m_items;       // guarded by m_mutex, size <= m_capacity
    bool m_closed = false;       // guarded by m_mutex
    std::uint64_t m_dropped = 0; // guarded by m_mutex
};

} // namespace hio
