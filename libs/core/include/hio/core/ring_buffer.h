#pragma once

// Fixed-capacity ring buffer: when full, push() overwrites the oldest element.
//
// Bound: exactly `capacity` elements, allocated once in the constructor.
// Thread safety: none. The owner (one thread) is documented by the user of the type.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <utility>
#include <vector>

namespace hio {

template <class T> class RingBuffer {
public:
    // Contract: capacity >= 1 (a zero capacity terminates the process: it is a programming error).
    explicit RingBuffer(std::size_t capacity) : m_storage(checkedCapacity(capacity)) {}

    // Appends an element. Returns true when the oldest element was overwritten (dropped).
    bool push(T value) {
        const std::size_t cap = m_storage.size();
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
    [[nodiscard]] std::size_t capacity() const noexcept { return m_storage.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

    // 0 = oldest element. Out-of-range access is a contract violation and terminates the process
    // (checked in every build type, not only in hardened standard library builds).
    [[nodiscard]] const T& operator[](std::size_t index) const {
        return m_storage[physicalIndex(index)];
    }
    [[nodiscard]] T& operator[](std::size_t index) { return m_storage[physicalIndex(index)]; }

    // Removes all elements; the capacity and the dropped counter are kept.
    void clear() noexcept(std::is_nothrow_default_constructible_v<T> &&
                          std::is_nothrow_move_assignable_v<T>) {
        for (std::size_t i = 0; i < m_size; ++i) {
            m_storage[physicalIndex(i)] = T{};
        }
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
        return (m_head + index) % m_storage.size();
    }

    std::vector<T> m_storage;
    std::size_t m_head = 0;
    std::size_t m_size = 0;
    std::uint64_t m_dropped = 0;
};

} // namespace hio
