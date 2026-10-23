#pragma once

// LineAssembler - turns the raw bytes of one RTT channel into ConsoleLines
// (plan/features/0002-m1-jlink-rtt.md):
//   - splits at the byte level on '\n' and strips one trailing '\r';
//   - keeps an incomplete line (at most maxPartialBytes, then it is emitted as a line, cut at a
//     UTF-8 character boundary);
//   - decodes UTF-8 per complete line, replacing invalid sequences with U+FFFD, so multi-byte
//     characters split across reads survive;
//   - strips ANSI CSI sequences (ESC '[' parameters intermediates final);
//   - applies carriage returns like a terminal that redraws a line (Zephyr's shell redraws its
//     prompt with "\r" + erase-line): trailing '\r's are dropped, and the text after the last
//     remaining '\r' replaces what came before it (applyCarriageReturns);
//   - parses the Zephyr level tag <dbg|inf|wrn|err> of log lines; a log line without a tag that
//     has the Zephyr hexdump continuation format gets LogLevel::Hexdump (ADR-0171);
//   - (profile rule hashPrefixIsLog) routes shell lines starting with '#' to the Log channel.
//
// Owned by the session worker; not thread-safe.

#include "hio/device/console_line.h"

#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hio::device {

class LineAssembler {
public:
    static constexpr std::size_t maxPartialBytes = 4096;

    LineAssembler(Channel channel, bool hashPrefixIsLog);

    // Completed lines (seq 0; time = `now`; dir = Out).
    [[nodiscard]] std::vector<ConsoleLine> feed(std::span<const std::byte> bytes,
                                                std::chrono::system_clock::time_point now);
    // Drops an incomplete line (a new RTT attach starts from scratch, like rttt's cache reset).
    void reset() noexcept;
    [[nodiscard]] std::size_t pendingBytes() const noexcept { return m_partial.size(); }
    // M5 (plan/features/0011): read-only view of the raw bytes of the incomplete line (prompt
    // detection, ADR-0082); valid until the next feed() or reset().
    [[nodiscard]] std::string_view partial() const noexcept { return m_partial; }

private:
    [[nodiscard]] ConsoleLine makeLine(std::string_view raw,
                                       std::chrono::system_clock::time_point now) const;

    Channel m_channel;
    bool m_hashPrefixIsLog;
    std::string m_partial; // bytes of the incomplete line, <= maxPartialBytes
};

// Helpers (exposed for tests).
[[nodiscard]] std::string stripAnsiCsi(std::string_view bytes);
[[nodiscard]] std::string decodeUtf8Lossy(std::string_view bytes); // invalid -> U+FFFD
// Drops trailing '\r's, then keeps the text after the last '\r' (review fix 8, 2026-10-09).
[[nodiscard]] std::string applyCarriageReturns(std::string_view bytes);
[[nodiscard]] LogLevel parseZephyrLevel(std::string_view text);
// ADR-0171: the Zephyr LOG_HEXDUMP continuation format - leading whitespace, 1-16 groups of two
// hex digits each followed by a space (one extra space after the 8th group, padding spaces for a
// short last line), then '|' and at most 17 printable ASCII characters (16 bytes as characters,
// with the space Zephyr puts after the 8th). Nothing else matches.
[[nodiscard]] bool isHexdumpLine(std::string_view text) noexcept;

} // namespace hio::device
