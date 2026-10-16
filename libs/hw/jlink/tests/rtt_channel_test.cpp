// RttChannel on a GMock of IJLinkApi and simulated time: one test per rule of ADR-0034
// (plan/features/0002, phase B2).

#include "fake_clock.h"
#include "hio/core/error.h"
#include "hio/hw/jlink/rtt_channel.h"
#include "hio/hw/jlink/types.h"
#include "mock_jlink_api.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace {

using hio::ErrorCode;
using hio::jlink::RttBufferInfo;
using hio::jlink::RttChannel;
using hio::jlink::RttDirection;
using hio::jlink::RttNames;
using hio::jlink::test::FakeClock;
using hio::jlink::test::MockJLinkApi;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

const RttNames chesterNames{.terminal = "Terminal", .logger = "Logger"};

// A simulated control block whose content can change over (simulated) time.
class Target {
public:
    std::vector<RttBufferInfo> up{{.name = "Terminal", .size = 1024},
                                  {.name = "Logger", .size = 1024}};
    std::vector<RttBufferInfo> down{{.name = "Terminal", .size = 16}};
    bool found = true;

    void install(NiceMock<MockJLinkApi>& api) {
        ON_CALL(api, rttStart()).WillByDefault(Return(0));
        ON_CALL(api, rttBufferCount(_)).WillByDefault([this](RttDirection direction) {
            if (!found) {
                return -2;
            }
            return static_cast<int>(direction == RttDirection::Up ? up.size() : down.size());
        });
        ON_CALL(api, rttBufferInfo(_, _))
            .WillByDefault(
                [this](int index, RttDirection direction) -> std::expected<RttBufferInfo, int> {
                    const auto& buffers = direction == RttDirection::Up ? up : down;
                    if (!found || index < 0 || std::cmp_greater_equal(index, buffers.size())) {
                        return std::unexpected(-1);
                    }
                    return buffers.at(static_cast<std::size_t>(index));
                });
    }
};

class Rtt : public ::testing::Test {
protected:
    Rtt() { m_target.install(m_api); }

    NiceMock<MockJLinkApi>& api() { return m_api; }
    FakeClock& clock() { return m_clock; }
    Target& target() { return m_target; }
    RttChannel& rtt() { return m_rtt; }

private:
    NiceMock<MockJLinkApi> m_api;
    FakeClock m_clock;
    Target m_target;
    RttChannel m_rtt{m_api, m_clock};
};

TEST_F(Rtt, AttachResolvesBuffersByName) {
    const auto layout = rtt().attach(chesterNames, {});
    ASSERT_TRUE(layout) << layout.error().detail;
    EXPECT_EQ(layout->terminalUp.index, 0);
    EXPECT_EQ(layout->terminalUp.size, 1024U);
    const auto logger = layout->loggerUp;
    ASSERT_TRUE(logger.has_value());
    EXPECT_EQ(logger.value_or(hio::jlink::RttBuffer{}).index, 1);
    EXPECT_EQ(layout->terminalDown.index, 0);
    EXPECT_EQ(layout->terminalDown.size, 16U);
    EXPECT_TRUE(rtt().isAttached());
}

TEST_F(Rtt, BuffersMovedByAReflashAreFoundByName) {
    target().up = {{.name = "Logger", .size = 512}, {.name = "Terminal", .size = 256}};
    target().down = {{.name = "Other", .size = 8}, {.name = "Terminal", .size = 32}};
    const auto layout = rtt().attach(chesterNames, {});
    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->terminalUp.index, 1);
    EXPECT_EQ(layout->terminalUp.size, 256U);
    EXPECT_EQ(layout->loggerUp.value_or(hio::jlink::RttBuffer{}).index, 0);
    EXPECT_EQ(layout->terminalDown.index, 1);
    EXPECT_EQ(layout->terminalDown.size, 32U);
}

TEST_F(Rtt, DownBufferFallsBackToTheTerminalIndex) {
    target().down = {{.name = "", .size = 16}};
    const auto layout = rtt().attach(chesterNames, {});
    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->terminalDown.index, 0);
}

TEST_F(Rtt, CountsArePolledEvery100msUntilTheControlBlockIsFound) {
    target().found = false;
    int polls = 0;
    ON_CALL(api(), rttBufferCount(RttDirection::Up)).WillByDefault([&](RttDirection) {
        if (++polls == 5) {
            target().found = true;
        }
        return target().found ? 2 : -2;
    });
    const auto layout = rtt().attach(chesterNames, {});
    ASSERT_TRUE(layout);
    EXPECT_EQ(polls, 5);
    EXPECT_EQ(clock().slept(), std::chrono::milliseconds(400));
}

TEST_F(Rtt, StaleZeroSizeTerminalStopsRttAndSearchesAgain) {
    target().up[0].size = 0;
    int starts = 0;
    ON_CALL(api(), rttStart()).WillByDefault([&] {
        if (++starts == 3) {
            target().up[0].size = 1024; // the firmware initialized its control block
        }
        return 0;
    });
    EXPECT_CALL(api(), rttStop()).Times(2);
    const auto layout = rtt().attach(chesterNames, {});
    ASSERT_TRUE(layout) << layout.error().detail;
    EXPECT_EQ(starts, 3);
    EXPECT_EQ(clock().slept(), std::chrono::milliseconds(400)); // 2 x 200 ms between the searches
}

TEST_F(Rtt, TerminalNotYetRegisteredRestartsTheSearch) {
    target().up = {{.name = "Logger", .size = 1024}};
    int starts = 0;
    ON_CALL(api(), rttStart()).WillByDefault([&] {
        if (++starts == 2) {
            target().up.push_back({.name = "Terminal", .size = 1024});
        }
        return 0;
    });
    const auto layout = rtt().attach(chesterNames, {});
    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->terminalUp.index, 1);
    EXPECT_EQ(starts, 2);
}

TEST_F(Rtt, LoggerRegisteredLateIsAwaited) {
    target().up[1].size = 0;
    int counts = 0;
    ON_CALL(api(), rttBufferCount(RttDirection::Up)).WillByDefault([&](RttDirection) {
        if (++counts == 10) {
            target().up[1].size = 1024;
        }
        return 2;
    });
    EXPECT_CALL(api(), rttStop()).Times(0); // descriptors are read live, no restart
    const auto layout = rtt().attach(chesterNames, {});
    ASSERT_TRUE(layout);
    ASSERT_TRUE(layout->loggerUp.has_value());
    EXPECT_LT(clock().slept(), std::chrono::milliseconds(3000));
}

TEST_F(Rtt, MissingLoggerContinuesWithoutLogsAfter3s) {
    target().up = {{.name = "Terminal", .size = 1024}};
    const auto layout = rtt().attach(chesterNames, {});
    ASSERT_TRUE(layout);
    EXPECT_FALSE(layout->loggerUp.has_value());
    EXPECT_GE(clock().slept(), std::chrono::milliseconds(3000));
    EXPECT_LE(clock().slept(), std::chrono::milliseconds(3100));
}

TEST_F(Rtt, ZeroSizedLoggerContinuesWithoutLogsAfter3s) {
    target().up[1].size = 0;
    const auto layout = rtt().attach(chesterNames, {});
    ASSERT_TRUE(layout);
    EXPECT_FALSE(layout->loggerUp.has_value());
}

TEST_F(Rtt, NoLoggerNameNeedsNoWait) {
    const auto layout = rtt().attach(RttNames{.terminal = "Terminal", .logger = ""}, {});
    ASSERT_TRUE(layout);
    EXPECT_FALSE(layout->loggerUp.has_value());
    EXPECT_EQ(clock().slept(), std::chrono::milliseconds(0));
}

TEST_F(Rtt, NoControlBlockWithin15sIsRttNotFound) {
    target().found = false;
    const auto layout = rtt().attach(chesterNames, {});
    ASSERT_FALSE(layout);
    EXPECT_EQ(layout.error().code, ErrorCode::TargetRttNotFound);
    EXPECT_GE(clock().slept(), std::chrono::milliseconds(15000));
    EXPECT_LE(clock().slept(), std::chrono::milliseconds(15300));
    EXPECT_FALSE(rtt().isAttached());
}

// ADR-0095: the attach deadline scales with a fixed speed below 1000 kHz, capped at 60 s.
TEST(RttAttachDeadline, ScalesWithAFixedLowSpeed) {
    using hio::jlink::rttAttachDeadline;
    using std::chrono::milliseconds;
    EXPECT_EQ(rttAttachDeadline(std::nullopt), milliseconds(15000)); // auto
    EXPECT_EQ(rttAttachDeadline(4000), milliseconds(15000));
    EXPECT_EQ(rttAttachDeadline(1000), milliseconds(15000));
    EXPECT_EQ(rttAttachDeadline(999), milliseconds(15015)); // 15 000 x 1000 / 999, rounded down
    EXPECT_EQ(rttAttachDeadline(500), milliseconds(30000));
    EXPECT_EQ(rttAttachDeadline(250), milliseconds(60000));
    EXPECT_EQ(rttAttachDeadline(100), milliseconds(60000)); // capped
    EXPECT_EQ(rttAttachDeadline(5), milliseconds(60000));
    // Another base (the session tests' short timings) scales the same way.
    EXPECT_EQ(rttAttachDeadline(std::nullopt, milliseconds(400)), milliseconds(400));
    EXPECT_EQ(rttAttachDeadline(500, milliseconds(400)), milliseconds(800));
    EXPECT_EQ(rttAttachDeadline(5, milliseconds(400)), milliseconds(1600));
}

TEST(RttAttachDeadline, SlowSpeedIsAFixedSpeedBelow125kHz) {
    using hio::jlink::isSlowRttSpeed;
    EXPECT_FALSE(isSlowRttSpeed(std::nullopt));
    EXPECT_FALSE(isSlowRttSpeed(1000));
    EXPECT_FALSE(isSlowRttSpeed(125));
    EXPECT_TRUE(isSlowRttSpeed(124));
    EXPECT_TRUE(isSlowRttSpeed(5));
}

// ADR-0095: the target.rtt_not_found detail names the deadline and the speed.
TEST_F(Rtt, RttNotFoundDetailNamesTheSpeed) {
    target().found = false;
    const auto autoSpeed = rtt().attach(chesterNames, {});
    ASSERT_FALSE(autoSpeed);
    EXPECT_EQ(autoSpeed.error().detail, "no RTT control block found within 15000 ms at auto speed");

    hio::jlink::RttTiming timing;
    timing.attachDeadline = hio::jlink::rttAttachDeadline(100);
    RttChannel slow(api(), clock(), timing, 100);
    const auto before = clock().slept();
    const auto layout = slow.attach(chesterNames, {});
    ASSERT_FALSE(layout);
    EXPECT_EQ(layout.error().code, ErrorCode::TargetRttNotFound);
    EXPECT_EQ(layout.error().detail, "no RTT control block found within 60000 ms at 100 kHz");
    EXPECT_GE(clock().slept() - before, std::chrono::milliseconds(60000));
    EXPECT_LE(clock().slept() - before, std::chrono::milliseconds(60300));
}

TEST_F(Rtt, TerminalNeverReadyIsBufferNotFound) {
    target().up = {{.name = "Logger", .size = 1024}};
    const auto layout = rtt().attach(chesterNames, {});
    ASSERT_FALSE(layout);
    EXPECT_EQ(layout.error().code, ErrorCode::TargetRttBufferNotFound);
    EXPECT_EQ(layout.error().message, "RTT buffer \"Terminal\" not found.");
}

TEST_F(Rtt, RttStartFailure) {
    ON_CALL(api(), rttStart()).WillByDefault(Return(-1));
    const auto layout = rtt().attach(chesterNames, {});
    ASSERT_FALSE(layout);
    EXPECT_EQ(layout.error().code, ErrorCode::TargetRttNotFound);
}

TEST_F(Rtt, AttachHonoursStopAndStopsRtt) {
    target().found = false;
    std::stop_source stop;
    int polls = 0;
    ON_CALL(api(), rttBufferCount(_)).WillByDefault([&](RttDirection) {
        if (++polls == 3) {
            stop.request_stop();
        }
        return -2;
    });
    EXPECT_CALL(api(), rttStop()).Times(1);
    const auto layout = rtt().attach(chesterNames, stop.get_token());
    ASSERT_FALSE(layout);
    EXPECT_EQ(layout.error().code, ErrorCode::Cancelled);
}

TEST_F(Rtt, ReadReturnsBytesOrReadFailed) {
    ASSERT_TRUE(rtt().attach(chesterNames, {}));
    std::array<std::byte, 64> buffer{};
    EXPECT_CALL(api(), rttRead(0, _)).WillOnce(Return(5)).WillOnce(Return(-1));
    const auto bytes = rtt().read(0, buffer);
    ASSERT_TRUE(bytes);
    EXPECT_EQ(*bytes, 5U);
    const auto failed = rtt().read(0, buffer);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, ErrorCode::TargetRttReadFailed);
}

std::vector<std::byte> bytesOf(const std::string& text) {
    std::vector<std::byte> bytes;
    for (const char c : text) {
        bytes.push_back(static_cast<std::byte>(c));
    }
    return bytes;
}

TEST_F(Rtt, WriteIsChunkedByTheDownBufferSize) {
    ASSERT_TRUE(rtt().attach(chesterNames, {}));
    const std::vector<std::byte> data = bytesOf(std::string(40, 'x'));
    std::vector<std::size_t> chunks;
    EXPECT_CALL(api(), rttWrite(0, _))
        .Times(3)
        .WillRepeatedly([&](int, std::span<const std::byte> chunk) {
            chunks.push_back(chunk.size());
            return static_cast<int>(chunk.size());
        });
    ASSERT_TRUE(rtt().write(0, data, {}));
    EXPECT_EQ(chunks, (std::vector<std::size_t>{16, 16, 8}));
}

TEST_F(Rtt, PartialWritesContinueWithTheRest) {
    ASSERT_TRUE(rtt().attach(chesterNames, {}));
    const std::vector<std::byte> data = bytesOf("version\n");
    EXPECT_CALL(api(), rttWrite(0, _))
        .WillOnce(Return(3))
        .WillOnce([](int, std::span<const std::byte> chunk) {
            EXPECT_EQ(chunk.size(), 5U);
            return 5;
        });
    EXPECT_TRUE(rtt().write(0, data, {}));
}

TEST_F(Rtt, ZeroByteWritesAreRetriedEvery5ms) {
    ASSERT_TRUE(rtt().attach(chesterNames, {}));
    const std::vector<std::byte> data = bytesOf("version\n");
    EXPECT_CALL(api(), rttWrite(0, _)).WillOnce(Return(0)).WillOnce(Return(0)).WillOnce(Return(8));
    const auto before = clock().slept();
    EXPECT_TRUE(rtt().write(0, data, {}));
    EXPECT_EQ(clock().slept() - before, std::chrono::milliseconds(10));
}

TEST_F(Rtt, WriteTimesOutAfter2sNeverSpinsForever) {
    ASSERT_TRUE(rtt().attach(chesterNames, {}));
    ON_CALL(api(), rttWrite(_, _)).WillByDefault(Return(0));
    const auto before = clock().slept();
    const auto status = rtt().write(0, bytesOf("version\n"), {});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::TargetRttWriteTimeout);
    EXPECT_GE(clock().slept() - before, std::chrono::milliseconds(2000));
    EXPECT_LE(clock().slept() - before, std::chrono::milliseconds(2010));
}

TEST_F(Rtt, WriteErrorFromTheLibrary) {
    ASSERT_TRUE(rtt().attach(chesterNames, {}));
    ON_CALL(api(), rttWrite(_, _)).WillByDefault(Return(-1));
    const auto status = rtt().write(0, bytesOf("version\n"), {});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::TargetRttWriteFailed);
    EXPECT_EQ(status.error().message, "Writing to the device failed.");
}

TEST_F(Rtt, ZeroSizedDownBufferIsNotWritten) {
    target().down[0].size = 0;
    ASSERT_TRUE(rtt().attach(chesterNames, {}));
    EXPECT_CALL(api(), rttWrite(_, _)).Times(0);
    const auto status = rtt().write(0, bytesOf("version\n"), {});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::TargetRttBufferNotFound);
}

TEST_F(Rtt, WriteHonoursStop) {
    ASSERT_TRUE(rtt().attach(chesterNames, {}));
    std::stop_source stop;
    ON_CALL(api(), rttWrite(_, _)).WillByDefault([&](int, std::span<const std::byte>) {
        stop.request_stop();
        return 0;
    });
    const auto status = rtt().write(0, bytesOf("version\n"), stop.get_token());
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::Cancelled);
}

TEST_F(Rtt, StopCallsRttStopOnce) {
    ASSERT_TRUE(rtt().attach(chesterNames, {}));
    EXPECT_CALL(api(), rttStop()).Times(1);
    rtt().stop();
    rtt().stop();
    EXPECT_FALSE(rtt().isAttached());
}

} // namespace
