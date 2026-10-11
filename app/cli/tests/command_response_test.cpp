// The bounded response of `hio chester app command` (review R42).

#include "command_response.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

namespace {

using hio::cli::CommandResponse;

TEST(CommandResponse, KeepsTheFirst10000LinesOfOutputAndLog) {
    CommandResponse response;
    for (std::size_t i = 0; i < CommandResponse::maxLines; ++i) {
        const bool kept = i % 2 == 0 ? response.addOutput("out " + std::to_string(i))
                                     : response.addLog("log " + std::to_string(i));
        ASSERT_TRUE(kept) << i;
    }
    EXPECT_FALSE(response.truncated());
    EXPECT_FALSE(response.addOutput("one too many"));
    EXPECT_FALSE(response.addLog("one too many"));
    EXPECT_TRUE(response.truncated());
    EXPECT_EQ(response.lines(), CommandResponse::maxLines);
    EXPECT_EQ(response.output().size(), CommandResponse::maxLines / 2);
    EXPECT_EQ(response.output().front(), "out 0");
    EXPECT_EQ(response.log().back(), "log 9999");
}

TEST(CommandResponse, KeepsAtMost4MiBOfText) {
    CommandResponse response;
    const std::string line(4000, 'x');
    std::size_t kept = 0;
    while (response.addOutput(line)) {
        ++kept;
    }
    EXPECT_TRUE(response.truncated());
    EXPECT_EQ(kept, CommandResponse::maxBytes / line.size()); // 1048 lines
    EXPECT_LE(response.bytes(), CommandResponse::maxBytes);
    EXPECT_LT(response.lines(), CommandResponse::maxLines);
    // After the bound nothing more is collected, not even a line that would still fit.
    EXPECT_FALSE(response.addLog("short"));
    EXPECT_TRUE(response.log().empty());
}

TEST(CommandResponse, UntruncatedByDefault) {
    CommandResponse response;
    EXPECT_TRUE(response.addOutput("Firmware version: v3.1.0"));
    EXPECT_FALSE(response.truncated());
    EXPECT_EQ(response.bytes(), 24U);
}

} // namespace
