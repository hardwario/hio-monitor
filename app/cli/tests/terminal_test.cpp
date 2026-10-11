// StdinLineReader with an injected read function (review R27).

#include "terminal.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

using hio::cli::StdinLineReader;
using ReadStatus = StdinLineReader::ReadStatus;

// Polls the reader until it delivers an item or 10 s pass.
std::optional<std::optional<std::string>> next(StdinLineReader& reader) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto item = reader.poll()) {
            return item;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

TEST(StdinLineReader, DeliversLinesAndTheEndOfInput) {
    int calls = 0;
    StdinLineReader reader([&calls](std::span<char> buffer, std::size_t& count) {
        if (calls++ > 0) {
            count = 0;
            return ReadStatus::End;
        }
        constexpr std::string_view input = "first\r\nsecond";
        input.copy(buffer.data(), input.size());
        count = input.size();
        return ReadStatus::Data;
    });
    EXPECT_EQ(next(reader), std::optional<std::optional<std::string>>(std::string("first")));
    EXPECT_EQ(next(reader), std::optional<std::optional<std::string>>(std::string("second")));
    const auto end = next(reader);
    ASSERT_TRUE(end.has_value());
    EXPECT_FALSE(end.value_or(std::string("not the end")).has_value()); // end of input
}

// An exception in the reader thread is the end of input; it never escapes the thread.
TEST(StdinLineReader, ExceptionInTheReaderThreadEndsTheInput) {
    int calls = 0;
    StdinLineReader reader([&calls](std::span<char> buffer, std::size_t& count) -> ReadStatus {
        if (calls++ > 0) {
            throw std::runtime_error("read failed");
        }
        constexpr std::string_view input = "before\n";
        input.copy(buffer.data(), input.size());
        count = input.size();
        return ReadStatus::Data;
    });
    EXPECT_EQ(next(reader), std::optional<std::optional<std::string>>(std::string("before")));
    const auto end = next(reader);
    ASSERT_TRUE(end.has_value());
    EXPECT_FALSE(end.value_or(std::string("not the end")).has_value());
}

} // namespace
