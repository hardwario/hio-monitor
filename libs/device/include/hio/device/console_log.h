#pragma once

// ConsoleLog - the bounded store of console lines with monotonically increasing cursors
// (plan/features/0002-m1-jlink-rtt.md, ADR-0063).
//
// Bound: capacityPerChannel lines per channel (default 100 000); the oldest line of a channel is
// evicted when it is full. Memory is allocated as lines arrive (RingBuffer grows up to the
// capacity), so creating a log is cheap. Every appended line gets the next sequence number (seq,
// starting at 1), so a cursor (the seq of the last line a reader has seen, 0 = nothing) stays valid
// across evictions and clear(): linesSince(cursor) returns the newer lines still stored.
//
// The log may be provided by the session's owner (a GUI tab passes one log to all its sessions,
// ADR-0063) or created by the session. Thread safety: none; owner thread only (the thread of the
// DeviceSession facade). Sessions never touch it from their worker.

#include "hio/core/ring_buffer.h"
#include "hio/device/console_line.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace hio::device {

enum class ChannelMask : std::uint8_t {
    Shell = 1U << 0U,
    Log = 1U << 1U,
    All = Shell | Log,
};

[[nodiscard]] constexpr bool contains(ChannelMask mask, Channel channel) noexcept {
    const auto bit = static_cast<std::uint8_t>(channel == Channel::Shell ? ChannelMask::Shell
                                                                         : ChannelMask::Log);
    return (static_cast<std::uint8_t>(mask) & bit) != 0;
}

class ConsoleLog {
public:
    static constexpr std::size_t defaultCapacityPerChannel = 100'000;

    explicit ConsoleLog(std::size_t capacityPerChannel = defaultCapacityPerChannel);

    // Stores the line with the next sequence number and returns it (line.seq is overwritten).
    std::uint64_t append(ConsoleLine line);
    // A Direction::Marker line with the current time (ADR-0063; added by the GUI).
    std::uint64_t appendMarker(Channel channel, std::string text);

    // Stored lines with seq > cursor of the channels in `mask`, in seq order, at most maxLines
    // (the oldest ones first).
    [[nodiscard]] std::vector<ConsoleLine>
    linesSince(std::uint64_t cursor, ChannelMask mask = ChannelMask::All,
               std::size_t maxLines = std::numeric_limits<std::size_t>::max()) const;
    // The newest `count` stored lines of the channels in `mask`, in seq order.
    [[nodiscard]] std::vector<ConsoleLine> last(std::size_t count,
                                                ChannelMask mask = ChannelMask::All) const;

    // Removes the stored lines of the channels in `mask`; sequence numbers continue.
    void clear(ChannelMask mask);

    // seq of the newest line ever appended (0 = none); a cursor for "from now on".
    [[nodiscard]] std::uint64_t lastSeq() const noexcept { return m_lastSeq; }
    [[nodiscard]] std::size_t size(Channel channel) const noexcept;
    [[nodiscard]] std::size_t capacityPerChannel() const noexcept { return m_shell.capacity(); }
    // ADR-0155 (M6): lines ever appended to `channel` (markers included); neither eviction nor
    // clear() resets it.
    [[nodiscard]] std::uint64_t total(Channel channel) const noexcept;
    // ADR-0155: seq of the oldest stored line of `channel`; nullopt when it stores none.
    [[nodiscard]] std::optional<std::uint64_t> firstSeq(Channel channel) const noexcept;

private:
    [[nodiscard]] const RingBuffer<ConsoleLine>& buffer(Channel channel) const noexcept;
    [[nodiscard]] RingBuffer<ConsoleLine>& buffer(Channel channel) noexcept;

    RingBuffer<ConsoleLine> m_shell;
    RingBuffer<ConsoleLine> m_log;
    std::uint64_t m_lastSeq = 0;
    std::uint64_t m_shellTotal = 0; // ADR-0155
    std::uint64_t m_logTotal = 0;
};

} // namespace hio::device
