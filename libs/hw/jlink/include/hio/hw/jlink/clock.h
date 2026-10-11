#pragma once

// Time source for the J-Link adapter and the device session. Every timeout and retry interval of
// plan/features/0002-m1-jlink-rtt.md goes through a Clock, so unit tests can run the 15 s RTT
// deadline or the 3 s reconnect interval on simulated time.
//
// Thread safety: SystemClock may be used from any thread. A Clock is used by the thread that owns
// the object it was passed to (e.g. the session worker).

#include <chrono>
#include <stop_token>

namespace hio::jlink {

class Clock {
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    Clock() = default;
    virtual ~Clock() = default;
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;
    Clock(Clock&&) = delete;
    Clock& operator=(Clock&&) = delete;

    [[nodiscard]] virtual TimePoint now() const = 0;
    // Waits `duration`. Returns false (possibly early) when `stop` is requested.
    [[nodiscard]] virtual bool sleepFor(std::chrono::milliseconds duration,
                                        const std::stop_token& stop) = 0;
};

// std::chrono::steady_clock; sleeps wake up immediately on a stop request.
class SystemClock final : public Clock {
public:
    [[nodiscard]] static SystemClock& instance();

    [[nodiscard]] TimePoint now() const override;
    [[nodiscard]] bool sleepFor(std::chrono::milliseconds duration,
                                const std::stop_token& stop) override;
};

} // namespace hio::jlink
