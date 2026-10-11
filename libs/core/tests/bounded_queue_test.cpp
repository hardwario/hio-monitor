#include "hio/core/bounded_queue.h"
#include "hio/core/error.h"
#include "hio/core/result.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace {
using hio::BoundedQueue;
using hio::ErrorCode;
using hio::OverflowPolicy;

TEST(BoundedQueue, FifoOrder) {
    BoundedQueue<int> queue(4, OverflowPolicy::Reject);
    ASSERT_TRUE(queue.push(1));
    ASSERT_TRUE(queue.push(2));
    ASSERT_TRUE(queue.push(3));
    EXPECT_EQ(queue.size(), 3U);
    EXPECT_EQ(queue.pop({}, std::chrono::milliseconds(0)), 1);
    EXPECT_EQ(queue.pop({}, std::chrono::milliseconds(0)), 2);
    EXPECT_EQ(queue.pop({}, std::chrono::milliseconds(0)), 3);
    EXPECT_FALSE(queue.pop({}, std::chrono::milliseconds(0)).has_value());
}

TEST(BoundedQueue, RejectWhenFull) {
    BoundedQueue<int> queue(2, OverflowPolicy::Reject);
    ASSERT_TRUE(queue.push(1));
    ASSERT_TRUE(queue.push(2));
    const hio::Status status = queue.push(3);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, ErrorCode::InternalQueueFull);
    EXPECT_EQ(queue.size(), 2U);
    EXPECT_EQ(queue.droppedCount(), 0U);
}

TEST(BoundedQueue, DropOldestWhenFull) {
    BoundedQueue<int> queue(2, OverflowPolicy::DropOldest);
    for (int i = 1; i <= 5; ++i) {
        ASSERT_TRUE(queue.push(i));
    }
    EXPECT_EQ(queue.droppedCount(), 3U);
    EXPECT_EQ(queue.pop({}, std::chrono::milliseconds(0)), 4);
    EXPECT_EQ(queue.pop({}, std::chrono::milliseconds(0)), 5);
}

// M1 review R10 (2026-10-10): drop the oldest element matching the predicate, else the oldest.
TEST(BoundedQueue, DropOldestMatchingKeepsOtherElements) {
    BoundedQueue<int> queue(3, [](const int& value) { return value % 2 == 0; }); // evens may go
    ASSERT_TRUE(queue.push(1));
    ASSERT_TRUE(queue.push(2));
    ASSERT_TRUE(queue.push(3));
    ASSERT_TRUE(queue.push(5)); // full: drops 2, the oldest even
    EXPECT_EQ(queue.droppedCount(), 1U);
    ASSERT_TRUE(queue.push(7)); // full, no even left: drops the oldest (1)
    EXPECT_EQ(queue.droppedCount(), 2U);
    EXPECT_EQ(queue.pop({}, std::chrono::milliseconds(0)), 3);
    EXPECT_EQ(queue.pop({}, std::chrono::milliseconds(0)), 5);
    EXPECT_EQ(queue.pop({}, std::chrono::milliseconds(0)), 7);
    EXPECT_FALSE(queue.pop({}, std::chrono::milliseconds(0)).has_value());
}

TEST(BoundedQueue, PopTimesOut) {
    BoundedQueue<int> queue(1, OverflowPolicy::Block);
    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(queue.pop({}, std::chrono::milliseconds(50)).has_value());
    EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(45));
}

TEST(BoundedQueue, BlockWaitsForSpace) {
    BoundedQueue<int> queue(1, OverflowPolicy::Block);
    ASSERT_TRUE(queue.push(1));
    std::atomic<bool> pushed{false};
    std::jthread producer([&] {
        EXPECT_TRUE(queue.push(2));
        pushed = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(pushed.load());
    EXPECT_EQ(queue.pop({}, std::chrono::seconds(1)), 1);
    EXPECT_EQ(queue.pop({}, std::chrono::seconds(5)), 2);
    producer.join();
    EXPECT_TRUE(pushed.load());
}

TEST(BoundedQueue, BlockedPushIsCancelledByStopToken) {
    BoundedQueue<int> queue(1, OverflowPolicy::Block);
    ASSERT_TRUE(queue.push(1));
    std::stop_source stop;
    hio::Status result;
    std::jthread producer([&] { result = queue.push(2, stop.get_token()); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    stop.request_stop();
    producer.join();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
    EXPECT_EQ(queue.size(), 1U);
}

TEST(BoundedQueue, CloseWakesBlockedPushAndPop) {
    BoundedQueue<int> full(1, OverflowPolicy::Block);
    ASSERT_TRUE(full.push(1));
    BoundedQueue<int> empty(1, OverflowPolicy::Block);

    hio::Status pushResult;
    std::optional<int> popResult = 42;
    std::jthread producer([&] { pushResult = full.push(2); });
    std::jthread consumer([&] { popResult = empty.pop({}, std::chrono::hours(1)); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    full.close();
    empty.close();
    producer.join();
    consumer.join();

    ASSERT_FALSE(pushResult.has_value());
    EXPECT_EQ(pushResult.error().code, ErrorCode::Cancelled);
    EXPECT_FALSE(popResult.has_value());
    EXPECT_TRUE(full.isClosed());
}

TEST(BoundedQueue, ClosedQueueDeliversRemainingItems) {
    BoundedQueue<int> queue(3, OverflowPolicy::Block);
    ASSERT_TRUE(queue.push(1));
    queue.close();
    EXPECT_FALSE(queue.push(2).has_value());
    EXPECT_EQ(queue.pop({}, std::chrono::milliseconds(0)), 1);
    EXPECT_FALSE(queue.pop({}, std::chrono::seconds(1)).has_value());
}

TEST(BoundedQueue, PopIsCancelledByStopToken) {
    BoundedQueue<int> queue(1, OverflowPolicy::Block);
    std::stop_source stop;
    std::optional<int> result = 1;
    std::jthread consumer([&] { result = queue.pop(stop.get_token(), std::chrono::hours(1)); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    stop.request_stop();
    consumer.join();
    EXPECT_FALSE(result.has_value());
}

TEST(BoundedQueue, MoveOnlyElements) {
    BoundedQueue<std::unique_ptr<int>> queue(1, OverflowPolicy::Reject);
    ASSERT_TRUE(queue.push(std::make_unique<int>(7)));
    auto value = queue.pop({}, std::chrono::milliseconds(0));
    ASSERT_TRUE(value.has_value() && *value != nullptr);
    const int got = value ? **value : -1;
    EXPECT_EQ(got, 7);
}

// Run under TSan (linux-tsan preset): many producers and consumers on a small blocking queue.
TEST(BoundedQueue, StressManyProducersAndConsumers) {
    constexpr int producers = 4;
    constexpr int consumers = 4;
    constexpr int perProducer = 5000;
    BoundedQueue<std::int64_t> queue(16, OverflowPolicy::Block);
    std::atomic<std::int64_t> sum{0};
    std::atomic<int> received{0};

    {
        std::vector<std::jthread> threads;
        threads.reserve(consumers);
        for (int c = 0; c < consumers; ++c) {
            threads.emplace_back([&](const std::stop_token& stop) {
                while (true) {
                    auto value = queue.pop(stop, std::chrono::milliseconds(10));
                    if (value) {
                        sum += *value;
                        ++received;
                    } else if (queue.isClosed() && queue.size() == 0) {
                        return;
                    }
                }
            });
        }
        std::vector<std::jthread> writers;
        writers.reserve(producers);
        for (int p = 0; p < producers; ++p) {
            writers.emplace_back([&queue, p] {
                for (int i = 1; i <= perProducer; ++i) {
                    EXPECT_TRUE(queue.push((static_cast<std::int64_t>(p) * perProducer) + i));
                }
            });
        }
        writers.clear(); // joins the producers
        queue.close();
    } // joins the consumers

    constexpr std::int64_t total = static_cast<std::int64_t>(producers) * perProducer;
    EXPECT_EQ(received.load(), total);
    EXPECT_EQ(sum.load(), total * (total + 1) / 2);
    EXPECT_EQ(queue.droppedCount(), 0U);
}

using BoundedQueueDeathTest = ::testing::Test;

TEST_F(BoundedQueueDeathTest, ZeroCapacityAborts) {
    EXPECT_DEATH({ const BoundedQueue<int> queue(0, OverflowPolicy::Reject); }, "");
}

} // namespace
