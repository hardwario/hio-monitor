#pragma once

// Fixed-capacity ring buffer: when full, push() overwrites the oldest element.
//
// Bound: at most `capacity` elements. The storage grows with the elements (doubling, but never
// beyond `capacity`) instead of being allocated up front, so a large but mostly unused buffer
// (e.g. the 100 000-line console log) costs nothing until lines arrive (M1 review, 2026-10-09).
// Once full, no further allocation happens.
// Thread safety: none. The owner (one thread) is documented by the user of the type.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

namespace hio {

template <class T> class RingBuffer {
public:
    // Contract: capacity >= 1 (a zero capacity terminates the process: it is a programming error).
    explicit RingBuffer(std::size_t capacity) : m_capacity(checkedCapacity(capacity)) {}

    // Appends an element. Returns true when the oldest element was overwritten (dropped).
    bool push(T value) {
        const std::size_t cap = m_capacity;
        if (m_storage.size() < cap) {
            // Still growing: the elements are stored in order from index 0 (m_head == 0).
            if (m_storage.size() == m_storage.capacity()) {
                m_storage.reserve(std::min(cap, std::max<std::size_t>(16, m_storage.size() * 2)));
            }
            m_storage.push_back(std::move(value));
            ++m_size;
            return false;
        }
        const std::size_t slot = (m_head + m_size) % cap;
        m_storage[slot] = std::move(value);
        if (m_size < cap) {
            ++m_size;
            return false;
        }
        m_head = (m_head + 1) % cap;
        ++m_dropped;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

    // 0 = oldest element. Out-of-range access is a contract violation and terminates the process
    // (checked in every build type, not only in hardened standard library builds).
    [[nodiscard]] const T& operator[](std::size_t index) const {
        return m_storage[physicalIndex(index)];
    }
    [[nodiscard]] T& operator[](std::size_t index) { return m_storage[physicalIndex(index)]; }

    // Removes all elements and releases their storage; the capacity and the dropped counter are
    // kept.
    void clear() noexcept {
        std::vector<T>().swap(m_storage);
        m_head = 0;
        m_size = 0;
    }

    // Number of elements overwritten since construction.
    [[nodiscard]] std::uint64_t droppedCount() const noexcept { return m_dropped; }

private:
    static std::size_t checkedCapacity(std::size_t capacity) {
        if (capacity == 0) {
            std::abort(); // contract violation: RingBuffer capacity must be >= 1
        }
        return capacity;
    }

    [[nodiscard]] std::size_t physicalIndex(std::size_t index) const noexcept {
        if (index >= m_size) {
            std::abort(); // contract violation: RingBuffer index out of range
        }
        return (m_head + index) % m_capacity;
    }

    std::size_t m_capacity;
    std::vector<T> m_storage; // grows to m_capacity elements
    std::size_t m_head = 0;
    std::size_t m_size = 0;
    std::uint64_t m_dropped = 0;
};

} // namespace hio
