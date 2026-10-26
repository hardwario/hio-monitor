// FindFilterEngine (plan 0005, C2): plain / regex / case matching, invalid regex, level chips,
// markers, and the time-sliced LineScan on 100 000 lines (slices, cancellation, lines arriving
// during a scan, slice time).

#include "gui_test_support.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "viewmodels/find_filter_engine.h"

#include <QString>
#include <QStringLiteral>
#include <QStringView>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <ratio>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using hio::device::Channel;
using hio::device::ConsoleLine;
using hio::device::ConsoleLog;
using hio::device::Direction;
using hio::device::LogLevel;
using hio::gui::LineFilter;
using hio::gui::LineScan;
using hio::gui::MatchRange;
using hio::gui::TextMatcher;
using hio::gui::TextQuery;
using hio::gui::test::waitFor;
namespace level_bits = hio::gui::level_bits;

ConsoleLine line(std::string text, LogLevel level = LogLevel::None,
                 Direction dir = Direction::Out) {
    ConsoleLine result;
    result.channel = Channel::Log;
    result.dir = dir;
    result.level = level;
    result.time = std::chrono::system_clock::now();
    result.text = std::move(text);
    return result;
}

TextMatcher compile(const QString& text, bool regex = false, bool caseSensitive = false) {
    auto matcher = TextMatcher::compile(
        TextQuery{.text = text, .regex = regex, .caseSensitive = caseSensitive});
    EXPECT_TRUE(matcher.has_value());
    return matcher.value_or(TextMatcher());
}

TEST(TextMatcher, EmptyMatchesEverything) {
    const TextMatcher matcher = compile(QString());
    EXPECT_TRUE(matcher.isEmpty());
    EXPECT_TRUE(matcher.matches(u"anything"));
    EXPECT_TRUE(matcher.ranges(u"anything").empty());
}

TEST(TextMatcher, PlainTextIsCaseInsensitiveByDefault) {
    const TextMatcher matcher = compile(QStringLiteral("uplink"));
    EXPECT_TRUE(matcher.matches(u"<inf> ctr_cloud: Uplink started"));
    EXPECT_FALSE(matcher.matches(u"<inf> ctr_cloud: downlink"));
    // Regex characters are plain text without .*
    EXPECT_TRUE(compile(QStringLiteral("a.c")).matches(u"xa.cx"));
    EXPECT_FALSE(compile(QStringLiteral("a.c")).matches(u"abc"));
}

TEST(TextMatcher, CaseSensitive) {
    const TextMatcher matcher = compile(QStringLiteral("Uplink"), false, true);
    EXPECT_TRUE(matcher.matches(u"Uplink done"));
    EXPECT_FALSE(matcher.matches(u"uplink done"));
}

TEST(TextMatcher, RegularExpression) {
    const TextMatcher matcher = compile(QStringLiteral("retry [0-9]/3"), true);
    EXPECT_TRUE(matcher.matches(u"Uplink timeout, RETRY 2/3"));
    EXPECT_FALSE(matcher.matches(u"retry x/3"));
    EXPECT_FALSE(compile(QStringLiteral("RETRY"), true, true).matches(u"retry 1/3"));
}

TEST(TextMatcher, InvalidRegularExpressionIsReported) {
    const auto matcher = TextMatcher::compile(
        TextQuery{.text = QStringLiteral("ctr_("), .regex = true, .caseSensitive = false});
    ASSERT_FALSE(matcher.has_value());
    EXPECT_FALSE(matcher.error().isEmpty());
    // The same text without .* is fine.
    EXPECT_TRUE(
        TextMatcher::compile(
            TextQuery{.text = QStringLiteral("ctr_("), .regex = false, .caseSensitive = false})
            .has_value());
}

TEST(TextMatcher, RangesAreNonOverlappingAndBounded) {
    const TextMatcher plain = compile(QStringLiteral("aa"));
    EXPECT_EQ(plain.ranges(u"aaaa"), (std::vector<MatchRange>{{0, 2}, {2, 2}}));
    const TextMatcher regex = compile(QStringLiteral("a*"), true);
    EXPECT_EQ(regex.ranges(u"baab"), (std::vector<MatchRange>{{1, 2}})); // empty matches skipped
    const QString many(1000, QLatin1Char('x'));
    EXPECT_EQ(compile(QStringLiteral("x")).ranges(many).size(), TextMatcher::maxRangesPerLine);
}

TEST(LineFilter, LevelChipsMarkersAndUntaggedLines) {
    LineFilter filter;
    EXPECT_FALSE(filter.isActive());
    filter.levelMask = level_bits::all & ~level_bits::debug;
    EXPECT_TRUE(filter.isActive());
    EXPECT_FALSE(filter.passes(line("<dbg> x", LogLevel::Debug), u"<dbg> x"));
    EXPECT_TRUE(filter.passes(line("<inf> x", LogLevel::Info), u"<inf> x"));
    // No level tag: not affected by the chips.
    EXPECT_TRUE(filter.passes(line("continuation"), u"continuation"));
    filter.text = compile(QStringLiteral("nothing"));
    // Markers are always shown and never searched.
    const ConsoleLine marker = line("— connected —", LogLevel::None, Direction::Marker);
    EXPECT_TRUE(filter.passes(marker, u"— connected —"));
    EXPECT_FALSE(
        hio::gui::findMatches(compile(QStringLiteral("connected")), marker, u"— connected —"));
    EXPECT_TRUE(hio::gui::findMatches(compile(QStringLiteral("x")), line("x"), u"x"));
    EXPECT_FALSE(hio::gui::findMatches(TextMatcher(), line("x"), u"x")); // empty search: no match
}

class LineScanTest : public ::testing::Test {
protected:
    static constexpr std::size_t lineCount = 100'000;

    void SetUp() override {
        m_log = std::make_shared<ConsoleLog>();
        for (std::size_t i = 0; i < lineCount; ++i) {
            m_log->append(
                line("<inf> app: sample " + std::to_string(i) + (i % 100 == 0 ? " Uplink" : ""),
                     i % 4 == 0 ? LogLevel::Debug : LogLevel::Info));
            // A shell line in between: the scan reads only its channel.
            if (i % 1000 == 0) {
                ConsoleLine shell = line("uart:~$");
                shell.channel = Channel::Shell;
                m_log->append(std::move(shell));
            }
        }
    }

    [[nodiscard]] const std::shared_ptr<ConsoleLog>& log() const { return m_log; }

private:
    std::shared_ptr<ConsoleLog> m_log;
};

TEST_F(LineScanTest, ScansAllLinesInSlicesOf5000) {
    LineScan scan(log(), Channel::Log);
    ASSERT_EQ(scan.sliceLines(), 5000U);
    std::size_t seen = 0;
    std::uint64_t lastSeq = 0;
    bool ordered = true;
    std::size_t largestSlice = 0;
    bool done = false;
    scan.start(
        0, [this] { return log()->lastSeq(); },
        [&](std::span<const ConsoleLine> lines) {
            largestSlice = std::max(largestSlice, lines.size());
            for (const ConsoleLine& l : lines) {
                ordered = ordered && l.seq > lastSeq && l.channel == Channel::Log;
                lastSeq = l.seq;
            }
            seen += lines.size();
        },
        [&] { done = true; });
    EXPECT_TRUE(scan.running());
    EXPECT_EQ(seen, 0U); // the first slice runs on the next event-loop turn
    ASSERT_TRUE(waitFor([&] { return done; }));
    EXPECT_EQ(seen, lineCount);
    EXPECT_TRUE(ordered);
    EXPECT_EQ(largestSlice, 5000U);
    EXPECT_GE(scan.slicesRun(), lineCount / 5000);
    EXPECT_FALSE(scan.running());
}

TEST_F(LineScanTest, FilterAndSearchSlicesAreFast) {
    // Plan 0005: each slice < 8 ms in Release. Checked on the median slice in optimized builds
    // without sanitizers (the slowest one depends on the machine's load); both are printed.
    LineScan scan(log(), Channel::Log);
    const LineFilter filter{.text = compile(QStringLiteral("sample [0-9]*0"), true),
                            .levelMask = level_bits::all & ~level_bits::warning};
    const TextMatcher find = compile(QStringLiteral("uplink"));
    std::vector<std::chrono::nanoseconds> times;
    std::size_t shown = 0;
    std::size_t matches = 0;
    bool done = false;
    scan.start(
        0, [this] { return log()->lastSeq(); },
        [&](std::span<const ConsoleLine> lines) {
            const auto start = std::chrono::steady_clock::now();
            for (const ConsoleLine& l : lines) {
                const QString text = hio::gui::lineText(l);
                if (filter.passes(l, text)) {
                    ++shown;
                    matches += hio::gui::findMatches(find, l, text) ? 1 : 0;
                }
            }
            times.push_back(std::chrono::steady_clock::now() - start);
        },
        [&] { done = true; });
    ASSERT_TRUE(waitFor([&] { return done; }));
    EXPECT_GT(shown, 0U);
    EXPECT_GT(matches, 0U);
    ASSERT_FALSE(times.empty());
    std::ranges::sort(times);
    const std::chrono::nanoseconds median = times[times.size() / 2];
    std::cout << "[ perf     ] filter+search slice of 5000 lines: median "
              << std::chrono::duration<double, std::milli>(median).count() << " ms, slowest "
              << std::chrono::duration<double, std::milli>(times.back()).count() << " ms\n";
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__) && !__has_feature(address_sanitizer) &&      \
    !__has_feature(thread_sanitizer)
    EXPECT_LT(median, std::chrono::milliseconds(8));
#endif
}

TEST_F(LineScanTest, ANewStartCancelsTheOldScan) {
    LineScan scan(log(), Channel::Log);
    std::size_t oldSlices = 0;
    bool oldDone = false;
    scan.start(
        0, [this] { return log()->lastSeq(); }, [&](std::span<const ConsoleLine>) { ++oldSlices; },
        [&] { oldDone = true; });
    std::size_t seen = 0;
    bool done = false;
    scan.start(
        0, [this] { return log()->lastSeq(); },
        [&](std::span<const ConsoleLine> lines) { seen += lines.size(); }, [&] { done = true; });
    ASSERT_TRUE(waitFor([&] { return done; }));
    EXPECT_EQ(oldSlices, 0U);
    EXPECT_FALSE(oldDone);
    EXPECT_EQ(seen, lineCount);

    bool cancelledDone = false;
    scan.start(
        0, [this] { return log()->lastSeq(); }, [](std::span<const ConsoleLine>) {},
        [&] { cancelledDone = true; });
    scan.cancel();
    hio::gui::test::spin(std::chrono::milliseconds(50));
    EXPECT_FALSE(cancelledDone);
    EXPECT_FALSE(scan.running());
}

TEST_F(LineScanTest, LinesArrivingDuringTheScanAreCovered) {
    LineScan scan(log(), Channel::Log);
    std::size_t seen = 0;
    bool appended = false;
    bool done = false;
    scan.start(
        0, [this] { return log()->lastSeq(); },
        [&](std::span<const ConsoleLine> lines) {
            seen += lines.size();
            if (!appended) {
                appended = true;
                for (int i = 0; i < 10; ++i) {
                    log()->append(line("late"));
                }
            }
        },
        [&] { done = true; });
    ASSERT_TRUE(waitFor([&] { return done; }));
    // The log keeps its newest 100 000 lines: the 10 late lines evicted 10 old ones, which the
    // first slice had already read.
    EXPECT_EQ(seen, lineCount + 10);
}

} // namespace
