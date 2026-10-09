#include "hio/core/ring_buffer.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

TEST(RingBuffer, StartsEmpty) {
    const hio::RingBuffer<int> buffer(3);
    EXPECT_EQ(buffer.size(), 0U);
    EXPECT_EQ(buffer.capacity(), 3U);
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.droppedCount(), 0U);
}

TEST(RingBuffer, KeepsOrderUntilFull) {
    hio::RingBuffer<int> buffer(3);
    EXPECT_FALSE(buffer.push(1));
    EXPECT_FALSE(buffer.push(2));
    EXPECT_FALSE(buffer.push(3));
    ASSERT_EQ(buffer.size(), 3U);
    EXPECT_EQ(buffer[0], 1);
    EXPECT_EQ(buffer[1], 2);
    EXPECT_EQ(buffer[2], 3);
}

TEST(RingBuffer, OverwritesOldestWhenFull) {
    hio::RingBuffer<int> buffer(3);
    for (int i = 1; i <= 7; ++i) {
        const bool overwrote = buffer.push(i);
        EXPECT_EQ(overwrote, i > 3);
    }
    ASSERT_EQ(buffer.size(), 3U);
    EXPECT_EQ(buffer[0], 5);
    EXPECT_EQ(buffer[1], 6);
    EXPECT_EQ(buffer[2], 7);
    EXPECT_EQ(buffer.droppedCount(), 4U);
}

TEST(RingBuffer, CapacityOne) {
    hio::RingBuffer<std::string> buffer(1);
    EXPECT_FALSE(buffer.push("a"));
    EXPECT_TRUE(buffer.push("b"));
    ASSERT_EQ(buffer.size(), 1U);
    EXPECT_EQ(buffer[0], "b");
}

TEST(RingBuffer, ClearKeepsCapacityAndDropCount) {
    hio::RingBuffer<int> buffer(2);
    (void)buffer.push(1);
    (void)buffer.push(2);
    (void)buffer.push(3);
    buffer.clear();
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.capacity(), 2U);
    EXPECT_EQ(buffer.droppedCount(), 1U);
    (void)buffer.push(9);
    EXPECT_EQ(buffer[0], 9);
}

TEST(RingBuffer, MoveOnlyElements) {
    hio::RingBuffer<std::unique_ptr<int>> buffer(2);
    (void)buffer.push(std::make_unique<int>(1));
    (void)buffer.push(std::make_unique<int>(2));
    (void)buffer.push(std::make_unique<int>(3));
    EXPECT_EQ(*buffer[0], 2);
    EXPECT_EQ(*buffer[1], 3);
}

TEST(RingBuffer, MutableAccess) {
    hio::RingBuffer<int> buffer(2);
    (void)buffer.push(1);
    buffer[0] = 42;
    EXPECT_EQ(buffer[0], 42);
}

using RingBufferDeathTest = ::testing::Test;

TEST_F(RingBufferDeathTest, ZeroCapacityAborts) {
    EXPECT_DEATH({ const hio::RingBuffer<int> buffer(0); }, "");
}

TEST_F(RingBufferDeathTest, OutOfRangeIndexAborts) {
    EXPECT_DEATH(
        {
            hio::RingBuffer<int> buffer(4);
            (void)buffer.push(1);
            (void)buffer[1];
        },
        "");
}

} // namespace
