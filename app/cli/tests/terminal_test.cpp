// StdinLineReader with an injected read function (review R27).

#include "terminal.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using hio::cli::OverlongLinePolicy;
using hio::cli::StdinItem;
using hio::cli::StdinLineReader;
using hio::cli::StdinReaderOptions;
using ReadStatus = StdinLineReader::ReadStatus;
using Kind = StdinItem::Kind;

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

// Delivers `parts` one per read call, then the end of input.
StdinLineReader::ReadFunction chunks(std::vector<std::string> parts) {
    return [parts = std::move(parts), index = std::size_t{0}](std::span<char> buffer,
                                                              std::size_t& count) mutable {
        if (index >= parts.size()) {
            count = 0;
            return ReadStatus::End;
        }
        const std::string& part = parts[index++];
        part.copy(buffer.data(), part.size());
        count = part.size();
        return ReadStatus::Data;
    };
}

// take() until the End item (10 s at most).
std::vector<StdinItem> takeAll(StdinLineReader& reader) {
    std::vector<StdinItem> items;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto item = reader.take()) {
            items.push_back(std::move(*item));
            if (items.back().kind == Kind::End) {
                break;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return items;
}

StdinItem line(std::string text) { return StdinItem{.kind = Kind::Line, .text = std::move(text)}; }
const StdinItem overlong{.kind = Kind::Overlong, .text = {}};
const StdinItem endItem{.kind = Kind::End, .text = {}};

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

// ADR-0154: the defaults are the console's values.
TEST(StdinLineReader, DefaultOptionsAreTheConsoleValues) {
    const StdinReaderOptions options;
    EXPECT_EQ(options.maxLineBytes, StdinLineReader::maxLineBytes);
    EXPECT_EQ(options.maxQueuedLines, StdinLineReader::maxQueuedLines);
    EXPECT_EQ(options.maxQueuedBytes, 0U); // ADR-0163: the console has no byte budget
    EXPECT_EQ(options.overlong, OverlongLinePolicy::Truncate);
    EXPECT_FALSE(options.notify);
}

TEST(StdinLineReader, TruncateCutsLongLines) {
    StdinLineReader reader(StdinReaderOptions{.maxLineBytes = 4,
                                              .maxQueuedLines = 8,
                                              .maxQueuedBytes = 0,
                                              .overlong = OverlongLinePolicy::Truncate,
                                              .notify = {}},
                           chunks({"abcdefgh\nab", "cdef\r\nxy\n", "abcd\r\n", "tail"}));
    EXPECT_EQ(takeAll(reader), (std::vector<StdinItem>{line("abcd"), line("abcd"), line("xy"),
                                                       line("abcd"), line("tail"), endItem}));
}

TEST(StdinLineReader, DropAndReportReplacesLongLines) {
    StdinReaderOptions options{.maxLineBytes = 4,
                               .maxQueuedLines = 8,
                               .maxQueuedBytes = 0,
                               .overlong = OverlongLinePolicy::DropAndReport,
                               .notify = {}};
    StdinLineReader reader(std::move(options), chunks({"abcdefgh\nok\n", "abcd\r\nabcde\r\n", "ab",
                                                       "cd", "e\n", "abcd\n\n", "toolongatend"}));
    EXPECT_EQ(takeAll(reader),
              (std::vector<StdinItem>{overlong, line("ok"), line("abcd"), overlong, overlong,
                                      line("abcd"), line(""), overlong, endItem}));
}

TEST(StdinLineReader, DropAndReportLongLineAcrossManyReads) {
    std::vector<std::string> parts(100, std::string(1000, 'x'));
    parts.emplace_back("\nafter\n");
    StdinLineReader reader(StdinReaderOptions{.maxLineBytes = 4096,
                                              .maxQueuedLines = 4,
                                              .maxQueuedBytes = 0,
                                              .overlong = OverlongLinePolicy::DropAndReport,
                                              .notify = {}},
                           chunks(std::move(parts)));
    EXPECT_EQ(takeAll(reader), (std::vector<StdinItem>{overlong, line("after"), endItem}));
}

// poll() (the console API) skips Overlong items.
TEST(StdinLineReader, PollSkipsOverlongItems) {
    StdinLineReader reader(StdinReaderOptions{.maxLineBytes = 2,
                                              .maxQueuedLines = 8,
                                              .maxQueuedBytes = 0,
                                              .overlong = OverlongLinePolicy::DropAndReport,
                                              .notify = {}},
                           chunks({"long\nok\n"}));
    EXPECT_EQ(next(reader), std::optional<std::optional<std::string>>(std::string("ok")));
    const auto finished = next(reader);
    ASSERT_TRUE(finished.has_value());
    EXPECT_FALSE(finished.value_or(std::string("not the end")).has_value());
}

// ADR-0154 (TSan): the notify callback runs on the reader thread after each push; a full queue
// blocks the reader (backpressure) until the consumer takes items; nothing is lost or reordered.
TEST(StdinLineReader, NotifyAfterEachPushAndBlockingWhenFull) {
    constexpr int lineCount = 200;
    std::vector<std::string> parts;
    parts.reserve(lineCount);
    for (int i = 0; i < lineCount; ++i) {
        parts.push_back("line " + std::to_string(i) + "\n");
    }
    std::mutex mutex;
    std::condition_variable wake;
    int notified = 0; // guarded by mutex
    std::atomic<int> reads{0};
    StdinReaderOptions options{.maxLineBytes = 64,
                               .maxQueuedLines = 2,
                               .maxQueuedBytes = 0,
                               .overlong = OverlongLinePolicy::DropAndReport,
                               .notify = [&] {
                                   {
                                       const std::scoped_lock lock(mutex);
                                       ++notified;
                                   }
                                   wake.notify_one();
                               }};
    auto read = chunks(std::move(parts));
    StdinLineReader reader(std::move(options),
                           [&reads, read](std::span<char> buffer, std::size_t& count) mutable {
                               ++reads;
                               return read(buffer, count);
                           });
    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(wake.wait_for(lock, std::chrono::seconds(10), [&] { return notified >= 2; }));
    }
    // The queue is full: the reader waits in push() and reads nothing more.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    {
        const std::scoped_lock lock(mutex);
        EXPECT_EQ(notified, 2);
    }
    EXPECT_EQ(reads.load(), 3); // the third line waits for queue space

    std::vector<StdinItem> items;
    while (items.empty() || items.back().kind != Kind::End) {
        if (auto item = reader.take()) {
            items.push_back(std::move(*item));
            continue;
        }
        std::unique_lock lock(mutex);
        ASSERT_TRUE(wake.wait_for(lock, std::chrono::seconds(10),
                                  [&] { return std::cmp_greater(notified, items.size()); }));
    }
    ASSERT_EQ(items.size(), static_cast<std::size_t>(lineCount) + 1);
    for (int i = 0; i < lineCount; ++i) {
        EXPECT_EQ(items[static_cast<std::size_t>(i)], line("line " + std::to_string(i)));
    }
    const std::scoped_lock lock(mutex);
    EXPECT_EQ(notified, lineCount + 1); // every line and the end of input
}

// Destroying the reader while it waits for queue space (full queue, nobody takes) stops it.
TEST(StdinLineReader, DestructionWhileTheQueueIsFull) {
    std::atomic<int> notified{0};
    {
        StdinLineReader reader(StdinReaderOptions{.maxLineBytes = 64,
                                                  .maxQueuedLines = 1,
                                                  .maxQueuedBytes = 0,
                                                  .overlong = OverlongLinePolicy::Truncate,
                                                  .notify = [&notified] { ++notified; }},
                               chunks({"a\n", "b\n", "c\n"}));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (notified.load() < 1 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    EXPECT_EQ(notified.load(), 1);
}

} // namespace

namespace {

// ADR-0163 (TSan): the queue also blocks on its byte budget. Lines of 100 bytes, budget 250: two
// lines are queued (200 bytes), the third waits although the line bound (64) is far away; each
// take() lets the next line in. Nothing is lost or reordered.
TEST(StdinLineReader, BlocksOnTheByteBudget) {
    constexpr int lineCount = 6;
    std::vector<std::string> parts;
    parts.reserve(lineCount);
    for (int i = 0; i < lineCount; ++i) {
        parts.push_back(std::string(99, static_cast<char>('a' + i)) + std::to_string(i) + "\n");
    }
    std::mutex mutex;
    std::condition_variable wake;
    int notified = 0; // guarded by mutex
    StdinLineReader reader(StdinReaderOptions{.maxLineBytes = 4096,
                                              .maxQueuedLines = 64,
                                              .maxQueuedBytes = 250,
                                              .overlong = OverlongLinePolicy::DropAndReport,
                                              .notify =
                                                  [&] {
                                                      {
                                                          const std::scoped_lock lock(mutex);
                                                          ++notified;
                                                      }
                                                      wake.notify_one();
                                                  }},
                           chunks(std::move(parts)));
    const auto waitFor = [&](int count) {
        std::unique_lock lock(mutex);
        return wake.wait_for(lock, std::chrono::seconds(10), [&] { return notified >= count; });
    };
    ASSERT_TRUE(waitFor(2));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    {
        const std::scoped_lock lock(mutex);
        EXPECT_EQ(notified, 2); // the third line waits for bytes
    }
    std::vector<StdinItem> items;
    auto first = reader.take();
    ASSERT_TRUE(first.has_value());
    items.push_back(std::move(first).value_or(StdinItem{}));
    ASSERT_TRUE(waitFor(3)); // one line taken: room for one more
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        const std::scoped_lock lock(mutex);
        EXPECT_EQ(notified, 3);
    }
    for (StdinItem& item : takeAll(reader)) {
        items.push_back(std::move(item));
    }
    ASSERT_EQ(items.size(), static_cast<std::size_t>(lineCount) + 1);
    for (int i = 0; i < lineCount; ++i) {
        EXPECT_EQ(items[static_cast<std::size_t>(i)].text,
                  std::string(99, static_cast<char>('a' + i)) + std::to_string(i));
    }
    EXPECT_EQ(items.back(), endItem);
}

// A line longer than the whole budget is still delivered (the queue holds no line bytes then).
TEST(StdinLineReader, LineLongerThanTheBudgetPasses) {
    StdinLineReader reader(StdinReaderOptions{.maxLineBytes = 4096,
                                              .maxQueuedLines = 8,
                                              .maxQueuedBytes = 10,
                                              .overlong = OverlongLinePolicy::DropAndReport,
                                              .notify = {}},
                           chunks({std::string(100, 'x') + "\n", "short\n"}));
    EXPECT_EQ(takeAll(reader),
              (std::vector<StdinItem>{line(std::string(100, 'x')), line("short"), endItem}));
}

// Destroying the reader while it waits for budget bytes stops it promptly.
TEST(StdinLineReader, DestructionWhileWaitingForBytes) {
    std::atomic<int> notified{0};
    {
        StdinLineReader reader(StdinReaderOptions{.maxLineBytes = 64,
                                                  .maxQueuedLines = 8,
                                                  .maxQueuedBytes = 4,
                                                  .overlong = OverlongLinePolicy::Truncate,
                                                  .notify = [&notified] { ++notified; }},
                               chunks({"abc\n", "def\n", "ghi\n"}));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (notified.load() < 1 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    EXPECT_EQ(notified.load(), 1);
}

} // namespace
