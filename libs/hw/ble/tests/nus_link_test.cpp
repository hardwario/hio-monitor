// NusLink on a GMock IBleApi (plan/features/0011 Tests: chunking, write modes, timeouts, pairing
// wait; ADR-0082, ADR-0084).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/hw/ble/nus_link.h"
#include "hio/hw/ble/types.h"
#include "mock_ble_api.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using hio::ErrorCode;
using hio::ble::LinkId;
using hio::ble::LinkReady;
using hio::ble::LinkTiming;
using hio::ble::NusLink;
using hio::ble::WriteMode;
using hio::ble::test::HandlerLog;
using hio::ble::test::MockBleApi;
using hio::ble::test::RecordingHandler;
using hio::ble::test::runFor;
using hio::ble::test::runUntil;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using namespace std::chrono_literals;

struct Write {
    std::string bytes;
    WriteMode mode;
    std::uint64_t token;
};

class NusLinkTest : public ::testing::Test {
protected:
    void SetUp() override {
        ON_CALL(m_api, connect(_, _)).WillByDefault(Return(hio::Result<LinkId>(7)));
        ON_CALL(m_api, write(_, _, _, _))
            .WillByDefault([this](LinkId, std::span<const std::byte> chunk, WriteMode mode,
                                  std::uint64_t token) {
                std::string bytes;
                for (const std::byte b : chunk) {
                    bytes.push_back(static_cast<char>(b));
                }
                m_writes.push_back(Write{.bytes = std::move(bytes), .mode = mode, .token = token});
                return hio::Status{};
            });
    }

    std::unique_ptr<NusLink> makeLink(LinkTiming timing = {}) {
        return std::make_unique<NusLink>(m_api, "F1:22:33:44:55:66",
                                         hio::ble::NusUuids{.service = "s", .rx = "rx", .tx = "tx"},
                                         std::make_unique<RecordingHandler>(m_log), timing);
    }

    std::unique_ptr<NusLink> readyLink(std::uint16_t mtu, WriteMode mode = WriteMode::WithResponse,
                                       LinkTiming timing = {}) {
        auto link = makeLink(timing);
        link->connect(false);
        link->onLinkConnecting(true);
        link->onLinkReady(LinkReady{.mtu = mtu, .writeMode = mode});
        return link;
    }

    // Confirms writes one by one until the line(s) are written (confirming one may add the next).
    void confirmAll(NusLink& link) {
        std::size_t next = 0;
        while (next < m_writes.size()) {
            const std::uint64_t token = m_writes.at(next).token;
            link.onWriteDone(token, std::nullopt);
            ++next;
        }
    }

    [[nodiscard]] NiceMock<MockBleApi>& api() { return m_api; }
    [[nodiscard]] HandlerLog& log() { return *m_log; }
    [[nodiscard]] std::vector<Write>& writes() { return m_writes; }

private:
    NiceMock<MockBleApi> m_api;
    std::shared_ptr<HandlerLog> m_log = std::make_shared<HandlerLog>();
    std::vector<Write> m_writes;
};

TEST_F(NusLinkTest, ConnectsAndReportsReadyWithTheMtu) {
    EXPECT_CALL(api(), connect(std::string("F1:22:33:44:55:66"), _));
    auto link = readyLink(247);
    EXPECT_EQ(link->state(), NusLink::State::Ready);
    EXPECT_EQ(log().events, (std::vector<std::string>{"connecting", "ready 247"}));
}

TEST_F(NusLinkTest, SmallMtuIsRaisedToTheDefault) {
    auto link = readyLink(10);
    EXPECT_EQ(link->mtu(), 23);
}

class Chunking : public NusLinkTest,
                 public ::testing::WithParamInterface<std::pair<std::uint16_t, std::size_t>> {};

// Lines of 1, MTU - 3 and 4096 bytes at MTU 23 / 185 / 247 / 517: chunks of MTU - 3, in order,
// one in flight with response, the echo only after the last chunk.
TEST_P(Chunking, LineIsSplitAtMtuMinusThree) {
    const auto [mtu, lineBytes] = GetParam();
    auto link = readyLink(mtu);
    const std::size_t size = lineBytes == 0 ? static_cast<std::size_t>(mtu - 3) : lineBytes;
    std::string line;
    for (std::size_t i = 0; i < size; ++i) {
        line.push_back(static_cast<char>('a' + (i % 26)));
    }
    link->sendLine(line);
    const std::size_t payload = mtu - 3;
    const std::size_t total = size + 1; // "\n"
    const std::size_t expected = (total + payload - 1) / payload;
    std::string sent;
    for (std::size_t i = 0; i < expected; ++i) {
        ASSERT_EQ(writes().size(), i + 1) << "one chunk in flight";
        EXPECT_LE(writes()[i].bytes.size(), payload);
        EXPECT_EQ(writes()[i].mode, WriteMode::WithResponse);
        EXPECT_TRUE(log().written.empty()) << "echo before the last chunk";
        sent += writes()[i].bytes;
        link->onWriteDone(writes()[i].token, std::nullopt);
    }
    EXPECT_EQ(writes().size(), expected);
    EXPECT_EQ(sent, line + "\n");
    EXPECT_EQ(log().written, std::vector<std::string>{line});
}

INSTANTIATE_TEST_SUITE_P(NusLink, Chunking,
                         ::testing::Values(std::pair<std::uint16_t, std::size_t>{23, 1},
                                           std::pair<std::uint16_t, std::size_t>{23, 0},
                                           std::pair<std::uint16_t, std::size_t>{23, 4096},
                                           std::pair<std::uint16_t, std::size_t>{185, 1},
                                           std::pair<std::uint16_t, std::size_t>{185, 0},
                                           std::pair<std::uint16_t, std::size_t>{185, 4096},
                                           std::pair<std::uint16_t, std::size_t>{247, 0},
                                           std::pair<std::uint16_t, std::size_t>{247, 4096},
                                           std::pair<std::uint16_t, std::size_t>{517, 0},
                                           std::pair<std::uint16_t, std::size_t>{517, 4096}));

TEST_F(NusLinkTest, WithoutResponseKeepsAtMostSixteenChunksInFlight) {
    auto link = readyLink(23, WriteMode::WithoutResponse);
    link->sendLine(std::string(1000, 'x')); // 51 chunks of 20 bytes
    ASSERT_EQ(writes().size(), NusLink::maxChunksInFlight);
    EXPECT_EQ(writes().front().mode, WriteMode::WithoutResponse);
    link->onWriteDone(writes()[0].token, std::nullopt);
    EXPECT_EQ(writes().size(), NusLink::maxChunksInFlight + 1);
    for (std::size_t i = 1; i < writes().size(); ++i) {
        link->onWriteDone(writes()[i].token, std::nullopt);
    }
    EXPECT_EQ(log().written.size(), 1U);
}

TEST_F(NusLinkTest, LinesAreWrittenInOrder) {
    auto link = readyLink(247);
    link->sendLine("one");
    link->sendLine("two");
    ASSERT_EQ(writes().size(), 1U);
    confirmAll(*link);
    confirmAll(*link);
    EXPECT_EQ(log().written, (std::vector<std::string>{"one", "two"}));
}

TEST_F(NusLinkTest, LineWhileNotConnectedIsRejected) {
    auto link = makeLink();
    link->sendLine("version");
    EXPECT_EQ(log().rejected, std::vector<std::string>{"target.not_connected version"});
    EXPECT_TRUE(writes().empty());
}

TEST_F(NusLinkTest, TooLongLineIsRejected) {
    auto link = readyLink(247);
    link->sendLine(std::string(NusLink::maxLineBytes + 1, 'x'));
    ASSERT_EQ(log().rejected.size(), 1U);
    EXPECT_TRUE(log().rejected.front().starts_with("usage.invalid_argument"));
}

TEST_F(NusLinkTest, QueueIsBounded) {
    auto link = readyLink(247);
    for (std::size_t i = 0; i < NusLink::maxQueuedLines + 2; ++i) {
        link->sendLine("x");
    }
    EXPECT_EQ(log().rejected.size(), 2U);
    EXPECT_TRUE(log().rejected.front().starts_with("internal.queue_full"));
}

TEST_F(NusLinkTest, WriteTimeoutEndsTheLink) {
    auto link = readyLink(247, WriteMode::WithResponse,
                          LinkTiming{.connectDeadline = 20s,
                                     .pairingDeadline = 60s,
                                     .pairingPromptDelay = 2s,
                                     .writeTimeout = 50ms,
                                     .flushInterval = 250ms});
    EXPECT_CALL(api(), disconnect(7));
    link->sendLine("version");
    link->sendLine("queued");
    ASSERT_TRUE(runUntil([&] { return link->state() == NusLink::State::Down; }));
    EXPECT_EQ(log().events.back(), "down ble.write_timeout");
    EXPECT_TRUE(log().written.empty());
    // Every unwritten line ends with a rejection with the link's error, also the one in flight.
    EXPECT_EQ(log().rejected,
              (std::vector<std::string>{"ble.write_timeout version", "ble.write_timeout queued"}));
}

TEST_F(NusLinkTest, FailedWriteEndsTheLinkWithItsError) {
    auto link = readyLink(247);
    link->sendLine("version");
    link->onWriteDone(writes().at(0).token, hio::makeError(ErrorCode::BleNotPaired));
    EXPECT_EQ(log().events.back(), "down ble.not_paired");
    EXPECT_EQ(link->state(), NusLink::State::Down);
}

TEST_F(NusLinkTest, ConnectDeadlineIsConnectFailed) {
    auto link = makeLink(LinkTiming{.connectDeadline = 30ms,
                                    .pairingDeadline = 60s,
                                    .pairingPromptDelay = 2s,
                                    .writeTimeout = 2s,
                                    .flushInterval = 250ms});
    EXPECT_CALL(api(), disconnect(7));
    link->connect(false);
    ASSERT_TRUE(runUntil([&] { return link->state() == NusLink::State::Down; }));
    EXPECT_EQ(log().events.back(), "down ble.connect_failed");
}

// The pending CCCD write replaces the connect deadline (B12: the pairing deadline is far away, so
// waiting well beyond the connect deadline cannot race it).
TEST_F(NusLinkTest, PendingCccdExtendsTheDeadlineAndPrompts) {
    auto link = makeLink(LinkTiming{.connectDeadline = 30ms,
                                    .pairingDeadline = 60s,
                                    .pairingPromptDelay = 10ms,
                                    .writeTimeout = 2s,
                                    .flushInterval = 250ms});
    link->connect(false);
    link->onLinkConnecting(true);
    ASSERT_TRUE(runUntil([&] { return log().events.size() >= 2; }));
    EXPECT_EQ(log().events, (std::vector<std::string>{"connecting", "prompt"}));
    const auto started = std::chrono::steady_clock::now();
    ASSERT_TRUE(runUntil([&] { return std::chrono::steady_clock::now() - started >= 150ms; }));
    EXPECT_EQ(link->state(), NusLink::State::Connecting); // 5x the connect deadline
    link->onLinkReady(LinkReady{.mtu = 247, .writeMode = WriteMode::WithResponse});
    EXPECT_EQ(link->state(), NusLink::State::Ready);
}

TEST_F(NusLinkTest, PairingDeadlineEndsNotPaired) {
    auto link = makeLink(LinkTiming{.connectDeadline = 60s,
                                    .pairingDeadline = 30ms,
                                    .pairingPromptDelay = 60s,
                                    .writeTimeout = 2s,
                                    .flushInterval = 250ms});
    link->connect(false);
    link->onLinkConnecting(true);
    ASSERT_TRUE(runUntil([&] { return link->state() == NusLink::State::Down; }));
    EXPECT_EQ(log().events, (std::vector<std::string>{"connecting", "down ble.not_paired"}));
}

TEST_F(NusLinkTest, ReconnectRejectsTheLineInFlight) {
    auto link = readyLink(247);
    link->sendLine("version");
    ASSERT_EQ(writes().size(), 1U);
    link->connect(false); // explicit reconnect while "version" is being written (B10)
    EXPECT_EQ(log().rejected, std::vector<std::string>{"target.not_connected version"});
    EXPECT_TRUE(log().written.empty());
}

TEST_F(NusLinkTest, QuickCccdShowsNoPrompt) {
    auto link = makeLink(LinkTiming{.connectDeadline = 20s,
                                    .pairingDeadline = 60s,
                                    .pairingPromptDelay = 30ms,
                                    .writeTimeout = 2s,
                                    .flushInterval = 250ms});
    link->connect(false);
    link->onLinkConnecting(true);
    link->onLinkReady(LinkReady{.mtu = 247, .writeMode = WriteMode::WithResponse});
    runFor(80ms);
    EXPECT_EQ(log().events, (std::vector<std::string>{"connecting", "ready 247"}));
}

// No time for a retry before the deadline (H6): the first error is the final one, at once.
TEST_F(NusLinkTest, ConnectErrorIsReported) {
    EXPECT_CALL(api(), connect(_, _))
        .WillOnce(Return(hio::Result<LinkId>(hio::fail(ErrorCode::BleConnectFailed))));
    auto link = makeLink(LinkTiming{.connectDeadline = 500ms,
                                    .pairingDeadline = 60s,
                                    .pairingPromptDelay = 2s,
                                    .writeTimeout = 2s,
                                    .flushInterval = 250ms,
                                    .connectRetryDelay = 1s});
    link->connect(false);
    EXPECT_EQ(log().events.back(), "down ble.connect_failed");
    EXPECT_EQ(link->state(), NusLink::State::Down);
}

// HIL H6: a failed attempt (BlueZ UnknownError, at once or as a lost link) is tried again after
// connectRetryDelay; the handler sees only the final result.
TEST_F(NusLinkTest, FailedConnectAttemptIsRetried) {
    EXPECT_CALL(api(), connect(_, _))
        .WillOnce(Return(hio::Result<LinkId>(hio::fail(ErrorCode::BleConnectFailed))))
        .WillOnce(Return(hio::Result<LinkId>(7)))
        .WillOnce(Return(hio::Result<LinkId>(8)));
    auto link = makeLink(LinkTiming{.connectDeadline = 20s,
                                    .pairingDeadline = 60s,
                                    .pairingPromptDelay = 2s,
                                    .writeTimeout = 2s,
                                    .flushInterval = 250ms,
                                    .connectRetryDelay = 10ms,
                                    .connectBusyAttempt = 20s,
                                    .connectImmediateFailure = 0ms, // H6 only, no H8 backoff
                                    .connectBusyRetryDelay = 2s});
    link->connect(false);
    EXPECT_EQ(link->state(), NusLink::State::Connecting);
    ASSERT_TRUE(runUntil([&] { return link->link() == LinkId{7}; }));
    link->onLinkLost(hio::makeError(ErrorCode::BleConnectFailed, "UnknownError"));
    EXPECT_EQ(link->state(), NusLink::State::Connecting);
    ASSERT_TRUE(runUntil([&] { return link->link() == LinkId{8}; }));
    link->onLinkConnecting(true);
    link->onLinkReady(LinkReady{.mtu = 247, .writeMode = WriteMode::WithResponse});
    EXPECT_EQ(log().events, (std::vector<std::string>{"connecting", "ready 247"}));
}

TEST_F(NusLinkTest, ConnectRetriesRunOutAtTheDeadline) {
    int attempts = 0;
    EXPECT_CALL(api(), connect(_, _)).WillRepeatedly([&attempts](auto&&...) {
        ++attempts;
        return hio::Result<LinkId>(hio::fail(ErrorCode::BleConnectFailed));
    });
    auto link = makeLink(LinkTiming{.connectDeadline = 100ms,
                                    .pairingDeadline = 60s,
                                    .pairingPromptDelay = 2s,
                                    .writeTimeout = 2s,
                                    .flushInterval = 250ms,
                                    .connectRetryDelay = 20ms,
                                    .connectBusyAttempt = 20s,
                                    .connectImmediateFailure = 0ms, // H6 only, no H8 backoff
                                    .connectBusyRetryDelay = 2s});
    const auto started = std::chrono::steady_clock::now();
    link->connect(false);
    ASSERT_TRUE(runUntil([&] { return link->state() == NusLink::State::Down; }));
    EXPECT_GE(std::chrono::steady_clock::now() - started, 80ms);
    EXPECT_GE(attempts, 2);
    EXPECT_EQ(log().events, (std::vector<std::string>{"connecting", "down ble.connect_failed"}));
    const int final = attempts;
    runFor(60ms);
    EXPECT_EQ(attempts, final); // nothing after the final failure
}

// HIL H8: instant failures (BlueZ InProgress) back off with growing delays instead of a burst.
TEST_F(NusLinkTest, ImmediateConnectFailuresBackOff) {
    std::vector<std::chrono::steady_clock::time_point> attempts;
    EXPECT_CALL(api(), connect(_, _)).WillRepeatedly([&attempts](auto&&...) {
        attempts.push_back(std::chrono::steady_clock::now());
        return hio::Result<LinkId>(hio::fail(ErrorCode::BleConnectFailed));
    });
    auto link = makeLink(LinkTiming{.connectDeadline = 1000ms,
                                    .pairingDeadline = 60s,
                                    .pairingPromptDelay = 2s,
                                    .writeTimeout = 2s,
                                    .flushInterval = 250ms,
                                    .connectRetryDelay = 5ms, // not used for these
                                    .connectBusyAttempt = 20s,
                                    .connectImmediateFailure = 1s,
                                    .connectBusyRetryDelay = 50ms});
    link->connect(false);
    ASSERT_TRUE(runUntil([&] { return link->state() == NusLink::State::Down; }));
    // 0, 50, 150, 350, 750 ms; the next (1550 ms) would be past the deadline.
    EXPECT_EQ(attempts.size(), 5U);
    for (std::size_t i = 1; i < attempts.size(); ++i) {
        // Coarse QTimers may fire up to 5 % early.
        EXPECT_GE(attempts.at(i) - attempts.at(i - 1), 45ms * (1 << (i - 1)));
    }
    EXPECT_EQ(log().events, (std::vector<std::string>{"connecting", "down ble.connect_failed"}));
}

// HIL H8: an attempt that ran into BlueZ's D-Bus timeout is followed by the busy delay, not 1 s.
TEST_F(NusLinkTest, LongConnectAttemptIsFollowedByTheBusyDelay) {
    int attempts = 0;
    EXPECT_CALL(api(), connect(_, _)).WillRepeatedly([&attempts](auto&&...) {
        ++attempts;
        return hio::Result<LinkId>(static_cast<LinkId>(attempts));
    });
    auto link = makeLink(LinkTiming{.connectDeadline = 20s,
                                    .pairingDeadline = 60s,
                                    .pairingPromptDelay = 2s,
                                    .writeTimeout = 2s,
                                    .flushInterval = 250ms,
                                    .connectRetryDelay = 5ms,
                                    .connectBusyAttempt = 50ms,
                                    .connectImmediateFailure = 0ms,
                                    .connectBusyRetryDelay = 150ms});
    link->connect(false);
    runFor(60ms);
    link->onLinkLost(hio::makeError(ErrorCode::BleConnectFailed, "UnknownError"));
    const auto lost = std::chrono::steady_clock::now();
    ASSERT_TRUE(runUntil([&] { return attempts == 2; }));
    EXPECT_GE(std::chrono::steady_clock::now() - lost, 140ms);
    EXPECT_EQ(link->state(), NusLink::State::Connecting);
    EXPECT_EQ(log().events, std::vector<std::string>{"connecting"});
}

// Q9-fatal codes and a failure during the pairing wait (the CCCD write) end the connect at once.
TEST_F(NusLinkTest, FatalConnectErrorsAndThePairingWaitAreNotRetried) {
    const LinkTiming timing{.connectDeadline = 20s,
                            .pairingDeadline = 60s,
                            .pairingPromptDelay = 2s,
                            .writeTimeout = 2s,
                            .flushInterval = 250ms,
                            .connectRetryDelay = 10ms};
    EXPECT_CALL(api(), connect(_, _)).Times(2);
    auto link = makeLink(timing);
    link->connect(false);
    link->onLinkLost(hio::makeError(ErrorCode::BleServiceNotFound));
    EXPECT_EQ(log().events.back(), "down ble.service_not_found");
    link->connect(false);
    link->onLinkConnecting(true);
    link->onLinkLost(hio::makeError(ErrorCode::BleConnectFailed, "CCCD write refused"));
    EXPECT_EQ(log().events.back(), "down ble.connect_failed");
    runFor(50ms);
    EXPECT_EQ(link->state(), NusLink::State::Down);
    ::testing::Mock::VerifyAndClearExpectations(&api());
}

TEST_F(NusLinkTest, LostLinkIsNotDisconnectedAgain) {
    auto link = readyLink(247);
    EXPECT_CALL(api(), disconnect(_)).Times(0);
    link->onLinkLost(hio::makeError(ErrorCode::BleDisconnected));
    EXPECT_EQ(log().events.back(), "down ble.disconnected");
    ::testing::Mock::VerifyAndClearExpectations(&api());
}

TEST_F(NusLinkTest, ReconnectDisconnectsTheOldLinkFirst) {
    auto link = readyLink(247);
    EXPECT_CALL(api(), disconnect(7)).Times(2); // the reconnect, then the destructor
    link->connect(true);
    EXPECT_EQ(log().events.back(), "connecting auto");
    EXPECT_EQ(link->state(), NusLink::State::Connecting);
}

TEST_F(NusLinkTest, DataIsPassedToTheHandler) {
    auto link = readyLink(247);
    const std::string text = "bt_nus:~$ ";
    link->onLinkData(std::as_bytes(std::span(text.data(), text.size())));
    EXPECT_EQ(log().data, text);
}

TEST_F(NusLinkTest, LateWriteConfirmationOfAnOldLinkIsIgnored) {
    auto link = readyLink(247);
    link->sendLine("a");
    const std::uint64_t old = writes().at(0).token;
    link->onLinkLost(hio::makeError(ErrorCode::BleDisconnected));
    link->onWriteDone(old, std::nullopt);
    EXPECT_TRUE(log().written.empty());
}

TEST_F(NusLinkTest, FlushTimerAndCloseReachTheHandler) {
    auto link = makeLink(LinkTiming{.connectDeadline = 20s,
                                    .pairingDeadline = 60s,
                                    .pairingPromptDelay = 2s,
                                    .writeTimeout = 2s,
                                    .flushInterval = 10ms});
    ASSERT_TRUE(runUntil([&] { return log().flushes >= 2; }));
    link.reset();
    EXPECT_TRUE(log().closed);
}

} // namespace
