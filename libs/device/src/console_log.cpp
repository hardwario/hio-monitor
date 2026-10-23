#include "hio/device/console_log.h"

#include "hio/core/ring_buffer.h"
#include "hio/device/console_line.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hio::device {

namespace {

// Index of the first line with seq > cursor (lines in a ring are in seq order).
std::size_t firstAfter(const RingBuffer<ConsoleLine>& ring, std::uint64_t cursor) {
    std::size_t low = 0;
    std::size_t high = ring.size();
    while (low < high) {
        const std::size_t middle = low + ((high - low) / 2);
        if (ring[middle].seq <= cursor) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

} // namespace

ConsoleLog::ConsoleLog(std::size_t capacityPerChannel)
    : m_shell(capacityPerChannel), m_log(capacityPerChannel) {}

const RingBuffer<ConsoleLine>& ConsoleLog::buffer(Channel channel) const noexcept {
    return channel == Channel::Shell ? m_shell : m_log;
}

RingBuffer<ConsoleLine>& ConsoleLog::buffer(Channel channel) noexcept {
    return channel == Channel::Shell ? m_shell : m_log;
}

std::uint64_t ConsoleLog::append(ConsoleLine line) {
    line.seq = ++m_lastSeq;
    const Channel channel = line.channel;
    ++(channel == Channel::Shell ? m_shellTotal : m_logTotal);
    buffer(channel).push(std::move(line));
    return m_lastSeq;
}

std::uint64_t ConsoleLog::appendMarker(Channel channel, std::string text) {
    ConsoleLine line;
    line.channel = channel;
    line.dir = Direction::Marker;
    line.time = std::chrono::system_clock::now();
    line.text = std::move(text);
    return append(std::move(line));
}

std::vector<ConsoleLine> ConsoleLog::linesSince(std::uint64_t cursor, ChannelMask mask,
                                                std::size_t maxLines) const {
    std::size_t shell =
        contains(mask, Channel::Shell) ? firstAfter(m_shell, cursor) : m_shell.size();
    std::size_t log = contains(mask, Channel::Log) ? firstAfter(m_log, cursor) : m_log.size();
    std::vector<ConsoleLine> lines;
    lines.reserve(std::min(maxLines, (m_shell.size() - shell) + (m_log.size() - log)));
    while (lines.size() < maxLines && (shell < m_shell.size() || log < m_log.size())) {
        const bool takeShell =
            log >= m_log.size() || (shell < m_shell.size() && m_shell[shell].seq < m_log[log].seq);
        lines.push_back(takeShell ? m_shell[shell++] : m_log[log++]);
    }
    return lines;
}

std::vector<ConsoleLine> ConsoleLog::last(std::size_t count, ChannelMask mask) const {
    // Walk back from the newest lines of both channels.
    std::size_t shell = contains(mask, Channel::Shell) ? m_shell.size() : 0;
    std::size_t log = contains(mask, Channel::Log) ? m_log.size() : 0;
    std::vector<ConsoleLine> lines;
    lines.reserve(std::min(count, shell + log));
    while (lines.size() < count && (shell > 0 || log > 0)) {
        const bool takeShell =
            log == 0 || (shell > 0 && m_shell[shell - 1].seq > m_log[log - 1].seq);
        lines.push_back(takeShell ? m_shell[--shell] : m_log[--log]);
    }
    std::ranges::reverse(lines);
    return lines;
}

void ConsoleLog::clear(ChannelMask mask) {
    if (contains(mask, Channel::Shell)) {
        m_shell.clear();
    }
    if (contains(mask, Channel::Log)) {
        m_log.clear();
    }
}

std::size_t ConsoleLog::size(Channel channel) const noexcept { return buffer(channel).size(); }

std::uint64_t ConsoleLog::total(Channel channel) const noexcept {
    return channel == Channel::Shell ? m_shellTotal : m_logTotal;
}

std::optional<std::uint64_t> ConsoleLog::firstSeq(Channel channel) const noexcept {
    const RingBuffer<ConsoleLine>& ring = buffer(channel);
    if (ring.empty()) {
        return std::nullopt;
    }
    return ring[0].seq;
}

} // namespace hio::device
