#pragma once

// RttChannel - SEGGER RTT over an IJLinkApi, reproducing rttt 1.9.2 PyLinkRTTConnector (ADR-0034):
//
// attach(): RTT start -> poll the up/down buffer counts every pollInterval (overall deadline
//   attachDeadline; it scales with a fixed low J-Link speed, rttAttachDeadline(), ADR-0110) ->
//   resolve the buffers by name on every attach (a reflash may move them) ->
//   require a non-zero terminal buffer size; a zero-sized (or not yet registered) terminal buffer
//   means a stale control block: RTT stop, wait staleRetryDelay, search again. The logger buffer
//   is awaited at most loggerWait, then the channel continues without logs.
// read(): up to the given buffer (the caller reads up to the up-buffer size per cycle).
// write(): in chunks of the down-buffer size; a 0-byte write is retried every writeRetryInterval
//   up to writeTimeout (counted from the start of the write) -> target.rtt_write_timeout.
// stop(): RTT stop.
//
// Owned by the session thread; not thread-safe.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/hw/jlink/clock.h"
#include "hio/hw/jlink/jlink_api.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>

namespace hio::jlink {

// ADR-0110: the RTT attach deadline at auto speed and at a fixed speed of rttFullSpeedKhz or more.
inline constexpr std::chrono::milliseconds baseRttAttachDeadline{15000};
// Below this fixed speed the deadline grows as base x rttFullSpeedKhz / kHz ...
inline constexpr int rttFullSpeedKhz = 1000;
// ... up to its cap, reached at this speed: base x 1000 / 250 = 4 x base (60 s for 15 s).
inline constexpr int rttCapSpeedKhz = 250;
// Below this fixed speed a search of the whole RAM can take longer than the capped deadline
// (about 64 s at 100 kHz); the CLI warns before connecting.
inline constexpr int rttSlowSpeedKhz = 125;

struct RttTiming {
    // At auto speed or a fixed speed >= rttFullSpeedKhz; a session scales it with
    // rttAttachDeadline() for its speed.
    std::chrono::milliseconds attachDeadline{baseRttAttachDeadline};
    std::chrono::milliseconds pollInterval{100};
    std::chrono::milliseconds loggerWait{3000};
    std::chrono::milliseconds staleRetryDelay{200};
    std::chrono::milliseconds writeTimeout{2000};
    std::chrono::milliseconds writeRetryInterval{5};
};

// ADR-0110: the RTT attach deadline at the J-Link speed `fixedSpeedKhz` (nullopt = auto):
// `base` at auto and at rttFullSpeedKhz or more, otherwise base x rttFullSpeedKhz / kHz, capped at
// the value for rttCapSpeedKhz (15 s, 15 s x 1000 / kHz, at most 60 s for the production base).
[[nodiscard]] constexpr std::chrono::milliseconds
rttAttachDeadline(std::optional<int> fixedSpeedKhz,
                  std::chrono::milliseconds base = baseRttAttachDeadline) {
    if (!fixedSpeedKhz || *fixedSpeedKhz >= rttFullSpeedKhz) {
        return base;
    }
    const int kiloHertz = *fixedSpeedKhz < rttCapSpeedKhz ? rttCapSpeedKhz : *fixedSpeedKhz;
    return base * rttFullSpeedKhz / kiloHertz;
}

// ADR-0110: true for a fixed speed below rttSlowSpeedKhz, where the console may not be found
// within the capped deadline.
[[nodiscard]] constexpr bool isSlowRttSpeed(std::optional<int> fixedSpeedKhz) {
    return fixedSpeedKhz && *fixedSpeedKhz < rttSlowSpeedKhz;
}

// "100 kHz" / "auto speed": the speed in error details (English, not translated).
[[nodiscard]] std::string speedDetail(std::optional<int> fixedSpeedKhz);

struct RttNames {
    std::string terminal; // e.g. "Terminal"
    std::string logger;   // e.g. "Logger"; empty = the firmware has no logger buffer
};

struct RttBuffer {
    int index = -1;
    std::uint32_t size = 0;
};

struct RttLayout {
    RttBuffer terminalUp;
    RttBuffer terminalDown;
    std::optional<RttBuffer> loggerUp; // nullopt: continuing without logs
    int upCount = 0;
    int downCount = 0;
};

class RttChannel {
public:
    // `speedKhz`: the session's J-Link speed (nullopt = auto), named in the deadline error.
    RttChannel(IJLinkApi& api, Clock& clock, RttTiming timing = {},
               std::optional<int> speedKhz = std::nullopt);

    // Errors: target.rtt_not_found (no control block before the deadline, or RTT start failed;
    // the detail names the deadline and the speed, ADR-0110),
    // target.rtt_buffer_not_found (control block found, terminal buffer never registered),
    // cancelled (stop requested; RTT is stopped again).
    [[nodiscard]] Result<RttLayout> attach(const RttNames& names, const std::stop_token& stopToken);

    // Bytes read into `buffer`. Errors: target.rtt_read_failed.
    [[nodiscard]] Result<std::size_t> read(int index, std::span<std::byte> buffer);

    // Writes all of `data` to the down buffer `index`. Errors: target.rtt_write_timeout,
    // target.rtt_write_failed (the library reported an error),
    // target.rtt_buffer_not_found (the down buffer has size 0), cancelled.
    [[nodiscard]] Status write(int index, std::span<const std::byte> data,
                               const std::stop_token& stopToken);

    // RTT stop (also when attach() did not finish). No-op when not started.
    void stop();

    [[nodiscard]] bool isAttached() const noexcept { return m_attached; }
    [[nodiscard]] const RttLayout& layout() const noexcept { return m_layout; }

private:
    struct BufferCounts {
        int up = -1;
        int down = -1;
    };
    struct AttachState {
        Clock::TimePoint deadline;
        std::optional<Clock::TimePoint> loggerDeadline;
        bool sawControlBlock = false;
    };

    [[nodiscard]] std::string libraryDetail(std::string detail);
    // Steps of attach(): each returns `cancelled` (RTT stopped again) on a stop request.
    [[nodiscard]] Result<RttLayout> cancelAttach();
    // The buffer counts once the control block is found; nullopt at the deadline.
    [[nodiscard]] Result<std::optional<BufferCounts>> pollCounts(Clock::TimePoint deadline,
                                                                 const std::stop_token& stopToken);
    // The layout by name; nullopt when the search has to restart (stale or incomplete block).
    [[nodiscard]] Result<std::optional<RttLayout>> resolveLayout(const RttNames& names,
                                                                 BufferCounts counts,
                                                                 AttachState& state,
                                                                 const std::stop_token& stopToken);
    [[nodiscard]] Result<RttLayout> finishAttach(const RttNames& names, RttLayout layout);
    [[nodiscard]] Error deadlineError(const RttNames& names, bool sawControlBlock);

    IJLinkApi& m_api;
    Clock& m_clock;
    RttTiming m_timing;
    std::optional<int> m_speedKhz;
    RttLayout m_layout;
    std::string m_terminalName;
    bool m_started = false;
    bool m_attached = false;
};

} // namespace hio::jlink
