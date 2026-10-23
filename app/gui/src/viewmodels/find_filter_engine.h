#pragma once

// FindFilterEngine - the matching and the time-sliced scans of the console views
// (plan/features/0005-m3-console.md, C2; ADR-0045, ADR-0046, ADR-0056).
//
// Pure logic, no QML:
// - TextMatcher: plain text (default) or a regular expression, case-insensitive by default. An
//   empty query matches every line. An invalid regular expression is reported as text for the
//   inline message ("Invalid regular expression: %1"); the caller keeps its last valid matcher
//   (ADR-0046: "keeps the last valid result").
// - Everything is matched on the text a row shows (shownText(): "> " before a sent command, cut at
//   maxShownChars), so a counted match is always a highlighted one (review K5).
// - LineFilter: a TextMatcher plus the level chips (D I W E). Marker rows (ADR-0063) are excluded
//   from filtering (always shown) and never match a search; lines without a level tag are not
//   affected by the level chips.
// - LineScan: walks the lines of one channel of a ConsoleLog in slices of `sliceLines` lines per
//   event-loop turn (QTimer::singleShot(0)), so the GUI thread never stalls. The end of the scan
//   is re-read before every slice (`limit`): lines that arrive while a scan runs are covered by it.
//   A new start() or cancel() bumps the generation counter, so a slice already queued for an old
//   scan does nothing.
//
// Bounds: one scan per LineScan; each slice copies at most `sliceLines` lines (5 000).
// Ownership: values / Qt parent (LineScan). Thread safety: GUI thread only (as ConsoleLog).

#include "hio/device/console_line.h"
#include "hio/device/console_log.h"

#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QStringView>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace hio::gui {

// Bits of the level chips (ConsoleLineModel::levelMask).
namespace level_bits {
inline constexpr int debug = 1;
inline constexpr int info = 2;
inline constexpr int warning = 4;
inline constexpr int error = 8;
// 0021 (ADR-0171): Zephyr hexdump continuation lines (chip H), off by default.
inline constexpr int hexdump = 16;
inline constexpr int all = debug | info | warning | error | hexdump;
// The chips of a new view: everything but hexdump lines.
inline constexpr int defaults = debug | info | warning | error;
} // namespace level_bits

struct TextQuery {
    QString text;
    bool regex = false;
    bool caseSensitive = false;

    [[nodiscard]] bool operator==(const TextQuery&) const = default;
};

// The longest text a row shows (the assembler's 4 KiB line limit); longer texts end with "…".
inline constexpr qsizetype maxShownChars = 4096;

// One match in a line: UTF-16 offset and length.
struct MatchRange {
    qsizetype start = 0;
    qsizetype length = 0;

    [[nodiscard]] bool operator==(const MatchRange&) const = default;
};

class TextMatcher {
public:
    static constexpr std::size_t maxRangesPerLine = 64;

    TextMatcher() = default; // empty: matches everything

    // The error is the regular expression's own error text (not translated, as Qt gives it).
    [[nodiscard]] static std::expected<TextMatcher, QString> compile(const TextQuery& query);

    [[nodiscard]] bool isEmpty() const noexcept { return m_text.isEmpty(); }
    [[nodiscard]] bool matches(QStringView text) const;
    // A match that can be highlighted: like matches(), but a regular expression must match at
    // least one character somewhere (ranges() skips empty matches such as "a*" on "xyz").
    [[nodiscard]] bool hasVisibleMatch(QStringView text) const;
    // At most `maxRanges` non-overlapping ranges in text order; none for an empty matcher.
    [[nodiscard]] std::vector<MatchRange> ranges(QStringView text,
                                                 std::size_t maxRanges = maxRangesPerLine) const;

private:
    QString m_text;
    Qt::CaseSensitivity m_case = Qt::CaseInsensitive;
    std::optional<QRegularExpression> m_regex;
};

// The level bit of a line (0 for lines without a level tag).
[[nodiscard]] int levelBit(device::LogLevel level) noexcept;

struct LineFilter {
    TextMatcher text;
    int levelMask = level_bits::all;

    // A text query or at least one level chip switched off.
    [[nodiscard]] bool isActive() const noexcept {
        return !text.isEmpty() || (levelMask & level_bits::all) != level_bits::all;
    }
    // Markers always pass; lines without a level tag pass the chips.
    [[nodiscard]] bool passes(const device::ConsoleLine& line, QStringView shownText) const;
};

// The text of a line (UTF-8 decoded).
[[nodiscard]] QString lineText(const device::ConsoleLine& line);
// The text a row shows: lineText() with "> " before a sent command, cut at maxShownChars (+ "…").
// Filter, search and the highlighted ranges all use this text.
[[nodiscard]] QString shownText(const device::ConsoleLine& line);

// A search match: the line is not a marker and the (non-empty) matcher has a visible match in
// `shown` (shownText()).
[[nodiscard]] bool findMatches(const TextMatcher& find, const device::ConsoleLine& line,
                               QStringView shown);

class LineScan : public QObject {
    Q_OBJECT

public:
    static constexpr std::size_t defaultSliceLines = 5000;

    using SliceHandler = std::function<void(std::span<const device::ConsoleLine>)>;
    using DoneHandler = std::function<void()>;
    // The newest seq the scan has to cover; re-read before each slice.
    using LimitProvider = std::function<std::uint64_t()>;

    LineScan(std::shared_ptr<const device::ConsoleLog> log, device::Channel channel,
             QObject* parent = nullptr);

    // Cancels a running scan and starts a new one over the lines with seq > afterSeq. The first
    // slice runs on the next event-loop turn. `onDone` is called once at the end (not when
    // cancelled).
    void start(std::uint64_t afterSeq, LimitProvider limit, SliceHandler onSlice,
               DoneHandler onDone);
    void cancel();
    [[nodiscard]] bool running() const noexcept { return m_running; }

    void setSliceLines(std::size_t lines) noexcept { m_sliceLines = lines == 0 ? 1 : lines; }
    [[nodiscard]] std::size_t sliceLines() const noexcept { return m_sliceLines; }
    // Slices run by the last (or current) scan (tests).
    [[nodiscard]] std::size_t slicesRun() const noexcept { return m_slicesRun; }

private:
    void schedule();
    void runSlice(std::uint64_t generation);

    std::shared_ptr<const device::ConsoleLog> m_log;
    device::Channel m_channel;
    std::size_t m_sliceLines = defaultSliceLines;
    std::uint64_t m_generation = 0;
    std::uint64_t m_cursor = 0;
    LimitProvider m_limit;
    SliceHandler m_onSlice;
    DoneHandler m_onDone;
    bool m_running = false;
    std::size_t m_slicesRun = 0;
};

[[nodiscard]] device::ChannelMask maskOf(device::Channel channel) noexcept;

} // namespace hio::gui
