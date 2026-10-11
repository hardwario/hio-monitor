#include "hio/hw/jlink/clock.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>

namespace hio::jlink {

SystemClock& SystemClock::instance() {
    static SystemClock clock;
    return clock;
}

Clock::TimePoint SystemClock::now() const { return std::chrono::steady_clock::now(); }

bool SystemClock::sleepFor(std::chrono::milliseconds duration, const std::stop_token& stop) {
    if (stop.stop_requested()) {
        return false;
    }
    std::mutex mutex;
    std::condition_variable_any wakeUp;
    std::unique_lock lock(mutex);
    // The predicate never becomes true: this waits for the timeout or a stop request.
    (void)wakeUp.wait_for(lock, stop, duration, [] { return false; });
    return !stop.stop_requested();
}

} // namespace hio::jlink
