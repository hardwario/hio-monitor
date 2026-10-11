#pragma once

// Simulated time for unit tests: sleepFor() advances now() instantly, so 15 s RTT deadlines or
// 500 ms connect retries run in microseconds. Thread-safe (atomic), shared with libs/device tests.

#include "hio/hw/jlink/clock.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stop_token>

namespace hio::jlink::test {

class FakeClock final : public Clock {
public:
    [[nodiscard]] TimePoint now() const override {
        return TimePoint(std::chrono::milliseconds(m_now.load()));
    }

    [[nodiscard]] bool sleepFor(std::chrono::milliseconds duration,
                                const std::stop_token& stop) override {
        if (stop.stop_requested()) {
            return false;
        }
        m_now += duration.count();
        m_slept += duration.count();
        return true;
    }

    void advance(std::chrono::milliseconds duration) { m_now += duration.count(); }

    // Total time spent in sleepFor().
    [[nodiscard]] std::chrono::milliseconds slept() const {
        return std::chrono::milliseconds(m_slept.load());
    }

private:
    std::atomic<std::int64_t> m_now{1'000'000};
    std::atomic<std::int64_t> m_slept{0};
};

} // namespace hio::jlink::test
