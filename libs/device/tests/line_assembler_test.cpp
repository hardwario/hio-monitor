#include "hio/device/console_line.h"
#include "hio/device/line_assembler.h"

#include <gtest/gtest.h>

#include <chrono>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using hio::device::Channel;
using hio::device::ConsoleLine;
using hio::device::Direction;
using hio::device::LineAssembler;
using hio::device::LogLevel;

std::vector<ConsoleLine> feed(LineAssembler& assembler, std::string_view bytes) {
    return assembler.feed(std::as_bytes(std::span(bytes.data(), bytes.size())),
                          std::chrono::system_clock::now());
}

std::vector<std::string> texts(const std::vector<ConsoleLine>& lines) {
    std::vector<std::string> result;
    result.reserve(lines.size());
    for (const ConsoleLine& line : lines) {
        result.push_back(line.text);
    }
    return result;
}

TEST(LineAssembler, PartialShowsTheIncompleteLine) {
    LineAssembler assembler(Channel::Shell, false);
    EXPECT_TRUE(assembler.partial().empty());
    EXPECT_EQ(texts(feed(assembler, "line\nbt_nus")), std::vector<std::string>{"line"});
    EXPECT_EQ(assembler.partial(), "bt_nus");
    EXPECT_TRUE(feed(assembler, ":~$ ").empty());
    EXPECT_EQ(assembler.partial(), "bt_nus:~$ ");
    assembler.reset();
    EXPECT_TRUE(assembler.partial().empty());
}

TEST(LineAssembler, SplitsLinesAndStripsCarriageReturn) {
    LineAssembler assembler(Channel::Shell, false);
    const auto lines = feed(assembler, "one\r\ntwo\nthree");
    EXPECT_EQ(texts(lines), (std::vector<std::string>{"one", "two"}));
    EXPECT_EQ(lines[0].channel, Channel::Shell);
    EXPECT_EQ(lines[0].dir, Direction::Out);
    EXPECT_EQ(lines[0].level, LogLevel::None);
    EXPECT_EQ(assembler.pendingBytes(), 5U);
    EXPECT_EQ(texts(feed(assembler, "\n")), (std::vector<std::string>{"three"}));
    EXPECT_EQ(texts(feed(assembler, "\r\n")), (std::vector<std::string>{""}));
}

TEST(LineAssembler, Utf8SplitAcrossReadsSurvives) {
    LineAssembler assembler(Channel::Shell, false);
    // "teplota 23 °C" with the two bytes of '°' (C2 B0) in different reads.
    EXPECT_TRUE(feed(assembler, "teplota 23 \xC2").empty());
    const auto lines = feed(assembler, "\xB0"
                                       "C\n");
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_EQ(lines[0].text, "teplota 23 \xC2\xB0"
                             "C");
}

TEST(LineAssembler, InvalidUtf8BecomesReplacementCharacter) {
    LineAssembler assembler(Channel::Shell, false);
    const auto lines = feed(assembler, "a\xFF"
                                       "b\n");
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_EQ(lines[0].text, "a\xEF\xBF\xBD"
                             "b");
}

TEST(LineAssembler, PartialLineIsForceEmittedAt4KiB) {
    LineAssembler assembler(Channel::Shell, false);
    const std::string longLine(LineAssembler::maxPartialBytes + 10, 'x');
    const auto lines = feed(assembler, longLine);
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_EQ(lines[0].text.size(), LineAssembler::maxPartialBytes);
    EXPECT_EQ(assembler.pendingBytes(), 10U);
}

TEST(LineAssembler, ForcedCutDoesNotSplitAUtf8Character) {
    LineAssembler assembler(Channel::Shell, false);
    std::string text(LineAssembler::maxPartialBytes - 1, 'x');
    text += "\xC3\xA9"; // 'é' crosses the 4 KiB boundary
    const auto lines = feed(assembler, text);
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_EQ(lines[0].text, std::string(LineAssembler::maxPartialBytes - 1, 'x'));
    const auto rest = feed(assembler, "\n");
    ASSERT_EQ(rest.size(), 1U);
    EXPECT_EQ(rest[0].text, "\xC3\xA9");
}

TEST(LineAssembler, AnsiCsiSequencesAreStripped) {
    LineAssembler assembler(Channel::Shell, false);
    const auto lines = feed(assembler, "\x1b[1;32muart:~$ \x1b[0mversion\x1b[K\n");
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_EQ(lines[0].text, "uart:~$ version");
    EXPECT_EQ(hio::device::stripAnsiCsi("a\x1b[2"), "a"); // cut sequence at the end
    EXPECT_EQ(hio::device::stripAnsiCsi("a\x1b"
                                        "b"),
              "a\x1b"
              "b"); // not CSI: kept
}

TEST(LineAssembler, ZephyrLevelOfLogLines) {
    LineAssembler assembler(Channel::Log, false);
    const auto lines = feed(assembler, "[00:00:01.234,000] <inf> app: Build version: v3.1.0\n"
                                       "[00:00:01.300,000] <wrn> lte: weak signal\n"
                                       "[00:00:01.400,000] <err> app: failed <dbg>\n"
                                       "[00:00:01.500,000] <dbg> x: y\n"
                                       "plain\n");
    ASSERT_EQ(lines.size(), 5U);
    EXPECT_EQ(lines[0].level, LogLevel::Info);
    EXPECT_EQ(lines[1].level, LogLevel::Warning);
    EXPECT_EQ(lines[2].level, LogLevel::Error); // the first tag counts
    EXPECT_EQ(lines[3].level, LogLevel::Debug);
    EXPECT_EQ(lines[4].level, LogLevel::None);
    EXPECT_EQ(lines[0].channel, Channel::Log);
}

TEST(LineAssembler, HashPrefixRoutesShellLinesToTheLog) {
    LineAssembler chester(Channel::Shell, true);
    const auto lines = feed(chester, "# [00:00:01.234] <inf> app: x\nuart:~$ ok\n");
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0].channel, Channel::Log);
    EXPECT_EQ(lines[0].level, LogLevel::Info);
    EXPECT_EQ(lines[1].channel, Channel::Shell);
    EXPECT_EQ(lines[1].level, LogLevel::None);

    LineAssembler other(Channel::Shell, false);
    EXPECT_EQ(feed(other, "# not a log\n")[0].channel, Channel::Shell);
}

TEST(LineAssembler, ShellLinesHaveNoLevel) {
    LineAssembler assembler(Channel::Shell, false);
    EXPECT_EQ(feed(assembler, "<err> looks like a log\n")[0].level, LogLevel::None);
}

// Review fix 8 (2026-10-09): embedded carriage returns, e.g. Zephyr shell prompt redraws.
TEST(LineAssembler, CarriageReturnsOverwriteLikeATerminal) {
    LineAssembler assembler(Channel::Shell, false);
    // Prompt redraw after a log message: "\r" + erase-line + prompt + echo.
    EXPECT_EQ(texts(feed(assembler, "\r\x1b[Juart:~$ version\r\n")),
              (std::vector<std::string>{"uart:~$ version"}));
    EXPECT_EQ(texts(feed(assembler, "uart:~$ \r\x1b[Kuart:~$ \n")),
              (std::vector<std::string>{"uart:~$ "}));
    EXPECT_EQ(texts(feed(assembler, "progress 10%\rprogress 50%\rdone\n")),
              (std::vector<std::string>{"done"}));
    // Only trailing carriage returns: the text before them is kept.
    EXPECT_EQ(texts(feed(assembler, "text\r\r\n")), (std::vector<std::string>{"text"}));
    EXPECT_EQ(hio::device::applyCarriageReturns("\r"), "");
    EXPECT_EQ(hio::device::applyCarriageReturns("a\rb"), "b");
}

TEST(LineAssembler, ResetDropsThePartialLine) {
    LineAssembler assembler(Channel::Shell, false);
    EXPECT_TRUE(feed(assembler, "stale").empty());
    assembler.reset();
    EXPECT_EQ(texts(feed(assembler, "fresh\n")), (std::vector<std::string>{"fresh"}));
}

TEST(ConsoleLineIds, Stable) {
    EXPECT_EQ(hio::device::channelId(Channel::Shell), "shell");
    EXPECT_EQ(hio::device::channelId(Channel::Log), "log");
    EXPECT_EQ(hio::device::directionId(Direction::In), "in");
    EXPECT_EQ(hio::device::directionId(Direction::Marker), "marker");
    EXPECT_EQ(hio::device::levelId(LogLevel::Warning), "wrn");
    EXPECT_FALSE(hio::device::levelId(LogLevel::None).has_value());
}

} // namespace

namespace {

// ADR-0082 / ADR-0104 (M6: public for the RTT prompt).
TEST(LineAssembler, IsPrompt) {
    using hio::device::isPrompt;
    EXPECT_TRUE(isPrompt("uart:~$ ", "uart:~$ "));
    EXPECT_TRUE(isPrompt("\x1b[1;32muart:~$ \x1b[m", "uart:~$ "));
    EXPECT_TRUE(isPrompt("junk\ruart:~$ ", "uart:~$ ")); // a redrawn line
    EXPECT_FALSE(isPrompt("uart:~$ ver", "uart:~$ "));
    EXPECT_FALSE(isPrompt("uart:~$ ", ""));
    EXPECT_FALSE(isPrompt("", "uart:~$ "));
}

} // namespace

// ---- plan 0021 (ADR-0171): hexdump continuation lines ---------------------------------------

namespace {

// Zephyr LOG_HEXDUMP_INF output as CHESTER prints it on the Logger channel (log_output.c
// hexdump_line_print: the prefix indentation, 16 "%02x " groups with an extra space after the 8th,
// '|', 16 characters with a space after the 8th; a short last line padded with spaces).
constexpr std::string_view recordedHexdump =
    "[00:00:12.345,000] <inf> app_lrw: Received:\r\n"
    "                                 44 74 4f 34 78 54 73 31  4b 36 69 41 73 41 53 4a "
    "|DtO4xTs1 K6iAsASJ\r\n"
    "                                 00 01 02 0a ff 7f 20 41  42 43                   "
    "|...... A BC      \r\n"
    "[00:00:12.346,000] <wrn> app_lrw: next\r\n";

TEST(Hexdump, ContinuationLinesGetTheirOwnLevel) {
    LineAssembler assembler(Channel::Log, false);
    const auto lines = feed(assembler, recordedHexdump);
    ASSERT_EQ(lines.size(), 4U);
    EXPECT_EQ(lines[0].level, LogLevel::Info); // the header keeps its level
    EXPECT_EQ(lines[1].level, LogLevel::Hexdump);
    EXPECT_EQ(lines[2].level, LogLevel::Hexdump);
    EXPECT_EQ(lines[3].level, LogLevel::Warning);
    EXPECT_EQ(lines[1].channel, Channel::Log);
    // The text is kept unchanged (search, copy, save).
    EXPECT_EQ(lines[1].text,
              "                                 44 74 4f 34 78 54 73 31  4b 36 69 41 "
              "73 41 53 4a |DtO4xTs1 K6iAsASJ");
}

TEST(Hexdump, ShellLinesAreNotClassified) {
    LineAssembler shell(Channel::Shell, false);
    const auto lines = feed(shell, "  44 74 |Dt\n");
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_EQ(lines[0].level, LogLevel::None);
    EXPECT_EQ(lines[0].channel, Channel::Shell);
}

TEST(Hexdump, HashPrefixedContinuation) {
    LineAssembler chester(Channel::Shell, true);
    const auto lines = feed(chester, "#   01 02 |..\n");
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_EQ(lines[0].channel, Channel::Log);
    EXPECT_EQ(lines[0].level, LogLevel::Hexdump);
}

TEST(Hexdump, NearMissesAreNotHexdumps) {
    using hio::device::isHexdumpLine;
    EXPECT_TRUE(isHexdumpLine("  44 |D"));
    EXPECT_TRUE(isHexdumpLine("\t44 74 |"));
    EXPECT_TRUE(
        isHexdumpLine("  44 74 4f 34 78 54 73 31  4b 36 69 41 73 41 53 4a |DtO4xTs1 K6iAsASJ"));
    EXPECT_FALSE(isHexdumpLine("44 74 |Dt"));                           // not indented
    EXPECT_FALSE(isHexdumpLine("  44 74"));                             // no '|'
    EXPECT_FALSE(isHexdumpLine("  |abc"));                              // no hex group
    EXPECT_FALSE(isHexdumpLine("  4 74 |Dt"));                          // one digit
    EXPECT_FALSE(isHexdumpLine("  447 4 |Dt"));                         // three digits
    EXPECT_FALSE(isHexdumpLine("  4g 74 |Dt"));                         // not hex
    EXPECT_FALSE(isHexdumpLine("  44 74 |DtO4xTs1 K6iAsASJ and more")); // too many characters
    EXPECT_FALSE(isHexdumpLine("  00000000: 44 74 |Dt"));               // shell_hexdump (offset)
    EXPECT_FALSE(isHexdumpLine("  value 44 |x"));                       // words
    EXPECT_FALSE(isHexdumpLine("  44 74 |D\x01"));                      // non-printable
    EXPECT_FALSE(isHexdumpLine(""));
    // 17 groups are not a hexdump line (16 bytes per line).
    EXPECT_FALSE(isHexdumpLine("  00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f 10 |x"));
}

TEST(Hexdump, LevelId) { EXPECT_EQ(hio::device::levelId(LogLevel::Hexdump), "hex"); }

} // namespace
