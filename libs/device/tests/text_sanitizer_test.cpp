// sanitizeDeviceText (ADR-0183, plan/features/0025 M-1): device text is terminal-safe.

#include "hwio/device/console_line.h"
#include "hwio/device/line_assembler.h"
#include "hwio/device/text_sanitizer.h"

#include <QString>
#include <QStringLiteral>

#include <gtest/gtest.h>

#include <chrono>
#include <span>
#include <string>
#include <string_view>

namespace {

using hwio::device::sanitizeDeviceText;
using namespace std::string_literals;

const std::string replacement = "\xEF\xBF\xBD"; // U+FFFD in UTF-8

std::string clean(std::string_view bytes) { return sanitizeDeviceText(bytes); }

TEST(TextSanitizer, CsiSequencesAreRemoved) {
    EXPECT_EQ(clean("\x1b[1;32muart:~$ \x1b[0mversion\x1b[K"), "uart:~$ version");
    EXPECT_EQ(clean("a\x1b[2"), "a"); // cut at the end
    EXPECT_EQ(clean("a\x1b[?25lb"), "ab");
}

TEST(TextSanitizer, OscEndsAtBelOrSt) {
    EXPECT_EQ(clean("a\x1b]0;evil title\x07z"), "az");
    EXPECT_EQ(clean("a\x1b]8;;https://x.example/\x1b\\link\x1b]8;;\x1b\\z"), "alinkz");
    EXPECT_EQ(clean("a\x1b]52;c;ZXZpbA==\x07z"), "az"); // clipboard write
}

TEST(TextSanitizer, DcsSosPmApcEndAtSt) {
    EXPECT_EQ(clean("a\x1bPq#0;2;0;0;0\x1b\\z"), "az");
    EXPECT_EQ(clean("a\x1bXsos\x1b\\b\x1b^pm\x1b\\c\x1b_apc\x1b\\d"), "abcd");
    EXPECT_EQ(clean("a\x1bPno bell\x07still dcs\x1b\\z"), "az"); // BEL does not end DCS
}

TEST(TextSanitizer, OtherEscapeSequencesAreRemoved) {
    EXPECT_EQ(clean("a\x1b"
                    "cb"),
              "ab");                    // full reset
    EXPECT_EQ(clean("a\x1b(Bb"), "ab"); // charset designation
    EXPECT_EQ(clean("a\x1b"
                    "7b\x1b"
                    "8c"),
              "abc");               // save / restore cursor
    EXPECT_EQ(clean("a\x1b"), "a"); // lone ESC at the end
}

TEST(TextSanitizer, C0ControlsExceptTabAndDelAreDropped) {
    EXPECT_EQ(clean("a\x0e"
                    "b\x0f"
                    "c"),
              "abc"); // SO / SI
    EXPECT_EQ(clean("a\x07"
                    "b"),
              "ab");                          // BEL
    EXPECT_EQ(clean("ab\x08\x08xy"), "abxy"); // BS
    EXPECT_EQ(clean("a\x7f"
                    "b"),
              "ab");                  // DEL
    EXPECT_EQ(clean("a\0b"s), "ab");  // NUL
    EXPECT_EQ(clean("a\tb"), "a\tb"); // TAB kept
}

TEST(TextSanitizer, C1ControlsBecomeReplacementCharacters) {
    // C2 9B is U+009B, which xterm-family terminals honour as CSI ("C2 9B 32 4A" clears the
    // screen).
    EXPECT_EQ(clean("a\xC2\x9B"
                    "2Jb"),
              "a" + replacement + "2Jb");
    EXPECT_EQ(clean("\xC2\x80\xC2\x9F"), replacement + replacement);
    EXPECT_EQ(clean("a\x9b"
                    "2J"),
              "a" + replacement + "2J"); // a raw 8-bit byte
}

TEST(TextSanitizer, Utf8IsKept) {
    EXPECT_EQ(clean("teplota 21,5 \xC2\xB0"
                    "C \xC4\x8D\xC5\x99 \xE2\x9C\x93"),
              "teplota 21,5 \xC2\xB0"
              "C \xC4\x8D\xC5\x99 \xE2\x9C\x93");
    EXPECT_EQ(clean("\xC2\xA0"), "\xC2\xA0"); // U+00A0 is not a C1 control
}

TEST(TextSanitizer, CarriageReturnsActLikeATerminal) {
    EXPECT_EQ(clean("progress 10%\rdone"), "done");
    EXPECT_EQ(clean("text\r\r"), "text");
}

TEST(TextSanitizer, UnterminatedOscSwallowsTheRestOfTheLine) {
    EXPECT_EQ(clean("\x1b]0;" + std::string(4000, 'x')), "");
    EXPECT_EQ(clean("\x1b]"), "");
}

TEST(TextSanitizer, QStringOverload) {
    EXPECT_EQ(sanitizeDeviceText(QStringLiteral("CHESTER\nfake line\x1b[2J\u009b")),
              QStringLiteral("CHESTERfake line�"));
}

// End to end: what LineAssembler emits for hostile device output.
TEST(TextSanitizer, LineAssemblerSanitisesEveryLine) {
    hwio::device::LineAssembler assembler(hwio::device::Channel::Shell, false);
    const std::string_view input = "\x1b]0;pwned\x07uart:~$ \x1b"
                                   "c\x0e"
                                   "ok\xC2\x9B"
                                   "2J\n"
                                   "\x1b]52;c;ZXZpbA==\x07\n"
                                   "\x1b]0;"
                                   "never ends\n";
    const auto lines =
        assembler.feed(std::as_bytes(std::span(input)), std::chrono::system_clock::now());
    ASSERT_EQ(lines.size(), 3U);
    EXPECT_EQ(lines[0].text, "uart:~$ ok" + replacement + "2J");
    EXPECT_EQ(lines[1].text, "");
    EXPECT_EQ(lines[2].text, "");
}

} // namespace
