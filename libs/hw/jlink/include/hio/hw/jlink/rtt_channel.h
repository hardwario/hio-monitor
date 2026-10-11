#pragma once

// RttChannel - SEGGER RTT over an IJLinkApi, reproducing rttt 1.9.2 PyLinkRTTConnector (ADR-0034):
//
// attach(): RTT start -> poll the up/down buffer counts every pollInterval (overall deadline
//   attachDeadline) -> resolve the buffers by name on every attach (a reflash may move them) ->
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

struct RttTiming {
    std::chrono::milliseconds attachDeadline{15000};
    std::chrono::milliseconds pollInterval{100};
    std::chrono::milliseconds loggerWait{3000};
    std::chrono::milliseconds staleRetryDelay{200};
    std::chrono::milliseconds writeTimeout{2000};
    std::chrono::milliseconds writeRetryInterval{5};
};

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
    RttChannel(IJLinkApi& api, Clock& clock, RttTiming timing = {});

    // Errors: target.rtt_not_found (no control block before the deadline, or RTT start failed),
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
    RttLayout m_layout;
    std::string m_terminalName;
    bool m_started = false;
    bool m_attached = false;
};

} // namespace hio::jlink
