#include "hio/device/line_assembler.h"

#include "hio/device/console_line.h"

#include <QByteArrayView>
#include <QString>
#include <QtTypes>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::device {

namespace {

constexpr char escape = '\x1b';

bool isContinuationByte(unsigned char c) { return (c & 0xC0U) == 0x80U; }

// Expected length of a UTF-8 sequence from its lead byte (1 for ASCII and invalid bytes).
std::size_t sequenceLength(unsigned char lead) {
    if ((lead & 0xE0U) == 0xC0U) {
        return 2;
    }
    if ((lead & 0xF0U) == 0xE0U) {
        return 3;
    }
    if ((lead & 0xF8U) == 0xF0U) {
        return 4;
    }
    return 1;
}

// The largest prefix length <= `limit` that does not end inside a UTF-8 sequence.
std::size_t utf8Boundary(std::string_view bytes, std::size_t limit) {
    std::size_t start = limit;
    // Walk back over at most 3 continuation bytes to the lead byte of the last character.
    for (std::size_t back = 0;
         back < 3 && start > 0 && isContinuationByte(static_cast<unsigned char>(bytes[start - 1]));
         ++back) {
        --start;
    }
    if (start == 0) {
        return limit;
    }
    const auto lead = static_cast<unsigned char>(bytes[start - 1]);
    const std::size_t needed = sequenceLength(lead);
    const std::size_t available = limit - (start - 1);
    return (needed > 1 && available < needed) ? start - 1 : limit;
}

} // namespace

std::string stripAnsiCsi(std::string_view bytes) {
    std::string result;
    result.reserve(bytes.size());
    std::size_t i = 0;
    while (i < bytes.size()) {
        if (bytes[i] == escape && i + 1 < bytes.size() && bytes[i + 1] == '[') {
            i += 2;
            while (i < bytes.size() && bytes[i] >= 0x30 && bytes[i] <= 0x3F) { // parameters
                ++i;
            }
            while (i < bytes.size() && bytes[i] >= 0x20 && bytes[i] <= 0x2F) { // intermediates
                ++i;
            }
            if (i < bytes.size() && bytes[i] >= 0x40 && bytes[i] <= 0x7E) { // final byte
                ++i;
            }
            continue;
        }
        result += bytes[i];
        ++i;
    }
    return result;
}

std::string decodeUtf8Lossy(std::string_view bytes) {
    return QString::fromUtf8(QByteArrayView(bytes.data(), static_cast<qsizetype>(bytes.size())))
        .toStdString();
}

std::string applyCarriageReturns(std::string_view bytes) {
    while (bytes.ends_with('\r')) {
        bytes.remove_suffix(1);
    }
    const std::size_t last = bytes.rfind('\r');
    if (last != std::string_view::npos) {
        bytes.remove_prefix(last + 1); // non-empty: trailing '\r's were removed above
    }
    return std::string(bytes);
}

LogLevel parseZephyrLevel(std::string_view text) {
    static constexpr std::array<std::pair<std::string_view, LogLevel>, 4> tags{{
        {"<dbg>", LogLevel::Debug},
        {"<inf>", LogLevel::Info},
        {"<wrn>", LogLevel::Warning},
        {"<err>", LogLevel::Error},
    }};
    std::size_t best = std::string_view::npos;
    LogLevel level = LogLevel::None;
    for (const auto& [tag, tagLevel] : tags) {
        const std::size_t position = text.find(tag);
        if (position < best) {
            best = position;
            level = tagLevel;
        }
    }
    return level;
}

bool isHexdumpLine(std::string_view text) noexcept {
    // Zephyr log_output.c hexdump_line_print(): the prefix indentation, then for each of the 16
    // positions "%02x " (or 3 spaces past the data), with one extra space before position 8; '|';
    // then the 16 characters (non-printable as '.', padded with spaces), a space before the 8th.
    constexpr std::size_t bytesPerLine = 16;
    constexpr std::size_t maxCharacters = bytesPerLine + 1;
    const auto isHex = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    };
    std::size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
        ++i;
    }
    if (i == 0) {
        return false; // the continuation is always indented
    }
    std::size_t groups = 0;
    while (groups < bytesPerLine && i + 2 < text.size() && isHex(text[i]) && isHex(text[i + 1]) &&
           text[i + 2] == ' ') {
        ++groups;
        i += 3;
        if (groups == bytesPerLine / 2 && i < text.size() && text[i] == ' ') {
            ++i; // the extra space after the 8th group
        }
    }
    if (groups == 0) {
        return false;
    }
    while (i < text.size() && text[i] == ' ') {
        ++i; // padding of a short last line
    }
    if (i >= text.size() || text[i] != '|') {
        return false;
    }
    const std::string_view characters = text.substr(i + 1);
    return characters.size() <= maxCharacters &&
           std::ranges::all_of(characters, [](char c) { return c >= 0x20 && c < 0x7F; });
}

LineAssembler::LineAssembler(Channel channel, bool hashPrefixIsLog)
    : m_channel(channel), m_hashPrefixIsLog(hashPrefixIsLog) {
    m_partial.reserve(maxPartialBytes);
}

void LineAssembler::reset() noexcept { m_partial.clear(); }

ConsoleLine LineAssembler::makeLine(std::string_view raw,
                                    std::chrono::system_clock::time_point now) const {
    if (raw.ends_with('\r')) {
        raw.remove_suffix(1);
    }
    ConsoleLine line;
    line.channel = m_channel;
    line.dir = Direction::Out;
    line.time = now;
    line.text = decodeUtf8Lossy(applyCarriageReturns(stripAnsiCsi(raw)));
    bool hashRouted = false;
    if (line.channel == Channel::Shell && m_hashPrefixIsLog && line.text.starts_with('#')) {
        line.channel = Channel::Log;
        hashRouted = true;
    }
    line.level = line.channel == Channel::Log ? parseZephyrLevel(line.text) : LogLevel::None;
    if (line.channel == Channel::Log && line.level == LogLevel::None &&
        isHexdumpLine(hashRouted ? std::string_view(line.text).substr(1) : line.text)) {
        line.level = LogLevel::Hexdump; // ADR-0171 (after the '#' of the hashPrefixIsLog rule)
    }
    return line;
}

std::vector<ConsoleLine> LineAssembler::feed(std::span<const std::byte> bytes,
                                             std::chrono::system_clock::time_point now) {
    std::vector<ConsoleLine> lines;
    for (const std::byte value : bytes) {
        const char c = static_cast<char>(value);
        if (c == '\n') {
            lines.push_back(makeLine(m_partial, now));
            m_partial.clear();
            continue;
        }
        m_partial += c;
        if (m_partial.size() >= maxPartialBytes) {
            // Bound reached: emit what we have, without splitting a UTF-8 character.
            const std::size_t cut = utf8Boundary(m_partial, m_partial.size());
            lines.push_back(makeLine(std::string_view(m_partial).substr(0, cut), now));
            m_partial.erase(0, cut);
        }
    }
    return lines;
}

} // namespace hio::device
