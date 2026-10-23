#include "hio/device/console_line.h"
#include "hio/device/console_log.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using hio::device::Channel;
using hio::device::ChannelMask;
using hio::device::ConsoleLine;
using hio::device::ConsoleLog;
using hio::device::Direction;

ConsoleLine line(Channel channel, std::string text) {
    ConsoleLine result;
    result.channel = channel;
    result.text = std::move(text);
    return result;
}

std::vector<std::string> texts(const std::vector<ConsoleLine>& lines) {
    std::vector<std::string> result;
    result.reserve(lines.size());
    for (const ConsoleLine& l : lines) {
        result.push_back(l.text);
    }
    return result;
}

TEST(ConsoleLog, SequenceNumbersAndMergedOrder) {
    ConsoleLog log(10);
    EXPECT_EQ(log.lastSeq(), 0U);
    EXPECT_EQ(log.append(line(Channel::Shell, "s1")), 1U);
    EXPECT_EQ(log.append(line(Channel::Log, "l1")), 2U);
    EXPECT_EQ(log.append(line(Channel::Shell, "s2")), 3U);
    EXPECT_EQ(texts(log.linesSince(0)), (std::vector<std::string>{"s1", "l1", "s2"}));
    EXPECT_EQ(texts(log.linesSince(1)), (std::vector<std::string>{"l1", "s2"}));
    EXPECT_EQ(texts(log.linesSince(0, ChannelMask::Log)), (std::vector<std::string>{"l1"}));
    EXPECT_EQ(texts(log.linesSince(0, ChannelMask::All, 2)),
              (std::vector<std::string>{"s1", "l1"}));
    EXPECT_EQ(log.linesSince(0)[2].seq, 3U);
    EXPECT_TRUE(log.linesSince(3).empty());
}

TEST(ConsoleLog, LastN) {
    ConsoleLog log(10);
    for (int i = 0; i < 5; ++i) {
        log.append(line(i % 2 == 0 ? Channel::Shell : Channel::Log, std::to_string(i)));
    }
    EXPECT_EQ(texts(log.last(3)), (std::vector<std::string>{"2", "3", "4"}));
    EXPECT_EQ(texts(log.last(2, ChannelMask::Shell)), (std::vector<std::string>{"2", "4"}));
    EXPECT_EQ(log.last(100).size(), 5U);
}

TEST(ConsoleLog, BoundedPerChannelCursorsStayValidAfterWrap) {
    ConsoleLog log(3);
    for (int i = 1; i <= 10; ++i) {
        log.append(line(Channel::Shell, std::to_string(i)));
    }
    log.append(line(Channel::Log, "log"));
    EXPECT_EQ(log.size(Channel::Shell), 3U);
    EXPECT_EQ(log.lastSeq(), 11U);
    // A reader at cursor 2 lost lines 3..7 (evicted) and continues with what is stored.
    EXPECT_EQ(texts(log.linesSince(2)), (std::vector<std::string>{"8", "9", "10", "log"}));
    EXPECT_EQ(texts(log.linesSince(9)), (std::vector<std::string>{"10", "log"}));
}

TEST(ConsoleLog, ClearKeepsCursorsMonotonic) {
    ConsoleLog log(10);
    log.append(line(Channel::Shell, "a"));
    log.append(line(Channel::Log, "b"));
    log.clear(ChannelMask::Shell);
    EXPECT_EQ(log.size(Channel::Shell), 0U);
    EXPECT_EQ(log.size(Channel::Log), 1U);
    EXPECT_EQ(log.append(line(Channel::Shell, "c")), 3U);
    EXPECT_EQ(texts(log.linesSince(0)), (std::vector<std::string>{"b", "c"}));
    log.clear(ChannelMask::All);
    EXPECT_TRUE(log.linesSince(0).empty());
    EXPECT_EQ(log.lastSeq(), 3U);
}

TEST(ConsoleLog, Markers) {
    ConsoleLog log(10);
    log.append(line(Channel::Shell, "out"));
    const std::uint64_t seq = log.appendMarker(Channel::Shell, "-- connected --");
    const auto lines = log.linesSince(0);
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[1].seq, seq);
    EXPECT_EQ(lines[1].dir, Direction::Marker);
    EXPECT_EQ(lines[1].text, "-- connected --");
}

TEST(ConsoleLog, DefaultCapacityIs100000PerChannel) {
    EXPECT_EQ(ConsoleLog::defaultCapacityPerChannel, 100000U);
    const ConsoleLog log;
    EXPECT_EQ(log.capacityPerChannel(), 100000U);
}

// ADR-0155 (M6): totals count every appended line per channel; eviction and clear() keep them.
TEST(ConsoleLog, TotalsAndFirstSeqAcrossEvictionAndClear) {
    ConsoleLog log(2);
    EXPECT_EQ(log.total(Channel::Shell), 0U);
    EXPECT_FALSE(log.firstSeq(Channel::Shell));
    (void)log.append(line(Channel::Shell, "s1"));           // seq 1
    (void)log.append(line(Channel::Log, "l1"));             // seq 2
    (void)log.append(line(Channel::Shell, "s2"));           // seq 3
    (void)log.appendMarker(Channel::Shell, "-- marker --"); // seq 4, evicts s1
    EXPECT_EQ(log.total(Channel::Shell), 3U);
    EXPECT_EQ(log.total(Channel::Log), 1U);
    EXPECT_EQ(log.size(Channel::Shell), 2U);
    EXPECT_EQ(log.firstSeq(Channel::Shell), std::optional<std::uint64_t>(3U));
    EXPECT_EQ(log.firstSeq(Channel::Log), std::optional<std::uint64_t>(2U));
    log.clear(ChannelMask::Shell);
    EXPECT_FALSE(log.firstSeq(Channel::Shell));
    EXPECT_EQ(log.total(Channel::Shell), 3U);
    (void)log.append(line(Channel::Shell, "s3")); // seq 5
    EXPECT_EQ(log.total(Channel::Shell), 4U);
    EXPECT_EQ(log.firstSeq(Channel::Shell), std::optional<std::uint64_t>(5U));
    EXPECT_EQ(log.firstSeq(Channel::Log), std::optional<std::uint64_t>(2U));
}

} // namespace
