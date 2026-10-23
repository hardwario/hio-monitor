// Offloader (ADR-0162): the running / queued bounds and mcp.too_many_requests, cancellation while
// queued and while running, a receiver destroyed before the result, stopAll() / the destructor
// joining, results on the owner thread, and a many-job stress test for TSan (linux-tsan).

#include "hio/core/error.h"
#include "hio/mcp/limits.h"
#include "hio/mcp/offload.h"
#include "mcp_test_support.h"

#include <QCoreApplication>
#include <QObject>
#include <QThread>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace hio::mcp {
namespace {

using namespace std::chrono_literals;
using test::runFor;
using test::runUntil;

// Shared between a test and its jobs (atomics only).
struct Gate {
    std::atomic<bool> open{false};
    std::atomic<int> started{0};
    std::atomic<int> stopped{0}; // jobs that returned because a stop was requested
    std::atomic<int> returned{0};
};

// A job that waits until the gate opens or a stop is requested; returns `value`.
std::function<int(std::stop_token)> waitingJob(const std::shared_ptr<Gate>& gate, int value) {
    return [gate, value](const std::stop_token& token) {
        ++gate->started;
        while (!gate->open.load() && !token.stop_requested()) {
            std::this_thread::sleep_for(1ms);
        }
        if (token.stop_requested()) {
            ++gate->stopped;
        }
        ++gate->returned;
        return value;
    };
}

TEST(Offloader, DefaultLimits) {
    const Limits limits;
    EXPECT_EQ(limits.maxOffloadRunning, 4U);
    EXPECT_EQ(limits.maxOffloadQueued, 16U);
}

TEST(Offloader, RunsFourQueuesSixteenRefusesTheTwentyFirst) {
    const Limits limits;
    Offloader offloader(limits.maxOffloadRunning, limits.maxOffloadQueued);
    QObject receiver;
    auto gate = std::make_shared<Gate>();
    std::vector<int> results;
    std::vector<Offloader::Handle> handles;
    for (int i = 0; i < 20; ++i) {
        auto handle = offloader.submit<int>(receiver, waitingJob(gate, i),
                                            [&results](int value) { results.push_back(value); });
        ASSERT_TRUE(handle.has_value()) << i;
        handles.push_back(std::move(*handle));
    }
    EXPECT_EQ(offloader.running(), 4U);
    EXPECT_EQ(offloader.queued(), 16U);
    auto refused = offloader.submit<int>(receiver, waitingJob(gate, 99), [](int) {});
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::McpTooManyRequests);
    ASSERT_TRUE(runUntil([&] { return gate->started.load() == 4; }));
    runFor(20ms);
    EXPECT_EQ(gate->started.load(), 4); // the queue waits for a free slot

    gate->open = true;
    ASSERT_TRUE(runUntil([&] { return results.size() == 20; }));
    EXPECT_EQ(offloader.running(), 0U);
    EXPECT_EQ(offloader.queued(), 0U);
    std::vector<int> sorted = results;
    std::ranges::sort(sorted);
    std::vector<int> expected;
    expected.reserve(20);
    for (int i = 0; i < 20; ++i) {
        expected.push_back(i);
    }
    EXPECT_EQ(sorted, expected);
    // The queue is FIFO: the queued jobs (4..19) start in order, so 4 finishes before 19.
    const auto position = [&results](int value) { return std::ranges::find(results, value); };
    EXPECT_LT(position(4), position(19));
}

TEST(Offloader, CancelWhileQueued) {
    Offloader offloader(1, 4);
    QObject receiver;
    auto gate = std::make_shared<Gate>();
    std::vector<int> results;
    auto first = offloader.submit<int>(receiver, waitingJob(gate, 1),
                                       [&results](int value) { results.push_back(value); });
    auto second = offloader.submit<int>(receiver, waitingJob(gate, 2),
                                        [&results](int value) { results.push_back(value); });
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(offloader.queued(), 1U);
    second->cancel();
    EXPECT_EQ(offloader.queued(), 0U);
    gate->open = true;
    ASSERT_TRUE(runUntil([&] { return offloader.running() == 0; }));
    runFor(20ms);
    EXPECT_EQ(results, std::vector<int>{1});
    EXPECT_EQ(gate->started.load(), 1); // the cancelled job never ran
}

TEST(Offloader, CancelWhileRunningDiscardsTheResult) {
    Offloader offloader(2, 2);
    QObject receiver;
    auto gate = std::make_shared<Gate>();
    bool called = false;
    auto handle =
        offloader.submit<int>(receiver, waitingJob(gate, 1), [&called](int) { called = true; });
    ASSERT_TRUE(handle.has_value());
    ASSERT_TRUE(runUntil([&] { return gate->started.load() == 1; }));
    handle->cancel();
    EXPECT_EQ(offloader.running(), 1U); // the slot is freed when the thread has ended
    ASSERT_TRUE(runUntil([&] { return offloader.running() == 0; }));
    runFor(20ms);
    EXPECT_EQ(gate->stopped.load(), 1);
    EXPECT_FALSE(called);

    // Destroying the handle cancels too.
    bool destroyedCalled = false;
    {
        auto scoped = offloader.submit<int>(receiver, waitingJob(gate, 2),
                                            [&destroyedCalled](int) { destroyedCalled = true; });
        ASSERT_TRUE(scoped.has_value());
        ASSERT_TRUE(runUntil([&] { return gate->started.load() == 2; }));
    }
    ASSERT_TRUE(runUntil([&] { return offloader.running() == 0; }));
    runFor(20ms);
    EXPECT_EQ(gate->stopped.load(), 2);
    EXPECT_FALSE(destroyedCalled);

    // A result that already arrived: cancel() is a no-op.
    std::optional<int> late;
    auto done = offloader.submit<int>(
        receiver, [](const std::stop_token&) { return 7; }, [&late](int value) { late = value; });
    ASSERT_TRUE(done.has_value());
    ASSERT_TRUE(runUntil([&] { return late.has_value(); }));
    done->cancel();
    EXPECT_EQ(late, 7);
}

TEST(Offloader, ReceiverDestroyedBeforeTheResult) {
    Offloader offloader(2, 2);
    auto receiver = std::make_unique<QObject>();
    auto gate = std::make_shared<Gate>();
    bool called = false;
    auto running =
        offloader.submit<int>(*receiver, waitingJob(gate, 1), [&called](int) { called = true; });
    ASSERT_TRUE(running.has_value());
    ASSERT_TRUE(runUntil([&] { return gate->started.load() == 1; }));
    receiver.reset();
    gate->open = true;
    ASSERT_TRUE(runUntil([&] { return offloader.running() == 0; }));
    runFor(20ms);
    EXPECT_FALSE(called);
    EXPECT_EQ(gate->returned.load(), 1);
}

TEST(Offloader, QueuedJobOfADestroyedReceiverNeverRuns) {
    Offloader offloader(1, 2);
    QObject keep;
    auto gone = std::make_unique<QObject>();
    auto gate = std::make_shared<Gate>();
    bool called = false;
    auto first = offloader.submit<int>(keep, waitingJob(gate, 1), [](int) {});
    auto second =
        offloader.submit<int>(*gone, waitingJob(gate, 2), [&called](int) { called = true; });
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    gone.reset();
    gate->open = true;
    ASSERT_TRUE(runUntil([&] { return offloader.running() == 0 && offloader.queued() == 0; }));
    runFor(20ms);
    EXPECT_FALSE(called);
    EXPECT_EQ(gate->started.load(), 1);
}

TEST(Offloader, StopAllRequestsStopAndJoins) {
    Offloader offloader(3, 4);
    QObject receiver;
    auto gate = std::make_shared<Gate>();
    int calls = 0;
    std::vector<Offloader::Handle> handles;
    for (int i = 0; i < 5; ++i) {
        auto handle =
            offloader.submit<int>(receiver, waitingJob(gate, i), [&calls](int) { ++calls; });
        ASSERT_TRUE(handle.has_value());
        handles.push_back(std::move(*handle));
    }
    ASSERT_TRUE(runUntil([&] { return gate->started.load() == 3; }));
    EXPECT_EQ(offloader.queued(), 2U);
    offloader.stopAll();
    // Joined: every running job has returned when stopAll() returns.
    EXPECT_EQ(gate->returned.load(), 3);
    EXPECT_EQ(gate->stopped.load(), 3);
    EXPECT_EQ(offloader.running(), 0U);
    EXPECT_EQ(offloader.queued(), 0U);
    runFor(20ms);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(gate->started.load(), 3); // the queued jobs were dropped

    // The offloader stays usable; the old handles are harmless.
    handles.clear();
    std::optional<int> value;
    auto again = offloader.submit<int>(
        receiver, [](const std::stop_token&) { return 5; },
        [&value](int result) { value = result; });
    ASSERT_TRUE(again.has_value());
    ASSERT_TRUE(runUntil([&] { return value.has_value(); }));
    EXPECT_EQ(value, 5);
}

TEST(Offloader, DestructorRequestsStopAndJoins) {
    auto gate = std::make_shared<Gate>();
    QObject receiver;
    std::vector<Offloader::Handle> survivors; // outlive the offloader
    {
        Offloader offloader(2, 2);
        for (int i = 0; i < 3; ++i) {
            auto handle = offloader.submit<int>(receiver, waitingJob(gate, i), [](int) {});
            ASSERT_TRUE(handle.has_value());
            survivors.push_back(std::move(*handle));
        }
        ASSERT_TRUE(runUntil([&] { return gate->started.load() == 2; }));
    }
    EXPECT_EQ(gate->returned.load(), 2);
    EXPECT_EQ(gate->stopped.load(), 2);
    EXPECT_EQ(gate->started.load(), 2); // the queued job was dropped
    survivors.clear();                  // the offloader is gone: cancel() is a no-op
    runFor(20ms);
}

TEST(Offloader, ResultsArriveOnTheOwnerThread) {
    Offloader offloader(2, 2);
    QObject receiver;
    QThread* const owner = QThread::currentThread();
    std::optional<bool> jobOnOwner;
    std::optional<bool> doneOnOwner;
    auto handle = offloader.submit<bool>(
        receiver, [owner](const std::stop_token&) { return QThread::currentThread() == owner; },
        [&](bool result) {
            jobOnOwner = result;
            doneOnOwner = QThread::currentThread() == owner;
        });
    ASSERT_TRUE(handle.has_value());
    // Not synchronous: the result needs the owner's event loop.
    EXPECT_FALSE(doneOnOwner.has_value());
    ASSERT_TRUE(runUntil([&] { return doneOnOwner.has_value(); }));
    EXPECT_EQ(jobOnOwner, false);
    EXPECT_EQ(doneOnOwner, true);
    EXPECT_EQ(QCoreApplication::instance()->thread(), owner);
}

TEST(Offloader, MoveOnlyResultsAndResubmitFromDone) {
    Offloader offloader(1, 1);
    QObject receiver;
    std::vector<int> received;
    std::vector<Offloader::Handle> handles;
    auto handle = offloader.submit<std::unique_ptr<int>>(
        receiver, [](const std::stop_token&) { return std::make_unique<int>(1); },
        [&](std::unique_ptr<int> value) {
            received.push_back(*value);
            // `done` may submit the next job.
            auto next = offloader.submit<std::unique_ptr<int>>(
                receiver, [](const std::stop_token&) { return std::make_unique<int>(2); },
                [&received](std::unique_ptr<int> second) { received.push_back(*second); });
            ASSERT_TRUE(next.has_value());
            handles.push_back(std::move(*next));
        });
    ASSERT_TRUE(handle.has_value());
    ASSERT_TRUE(runUntil([&] { return received.size() == 2; }));
    EXPECT_EQ(received, (std::vector<int>{1, 2}));
}

// Many short jobs with own data, random cancellation and a full queue: run under linux-tsan.
TEST(Offloader, ManyJobsStress) {
    const Limits limits;
    Offloader offloader(limits.maxOffloadRunning, limits.maxOffloadQueued);
    QObject receiver;
    constexpr int total = 300;
    int submitted = 0;
    int delivered = 0;
    int cancelled = 0;
    int refused = 0;
    long long sum = 0;
    long long expectedSum = 0;
    std::vector<Offloader::Handle> handles;
    while (submitted < total) {
        std::vector<int> data(static_cast<std::size_t>(64 + (submitted % 64)), submitted);
        const long long value =
            std::accumulate(data.begin(), data.end(), 0LL) + static_cast<long long>(data.size());
        auto handle = offloader.submit<long long>(
            receiver,
            [data = std::move(data)](const std::stop_token& token) mutable {
                long long result = 0;
                for (int& item : data) {
                    if (token.stop_requested()) {
                        return -1LL;
                    }
                    ++item;
                    result += item;
                }
                return result;
            },
            [&sum, &delivered](long long result) {
                sum += result;
                ++delivered;
            });
        if (!handle) {
            EXPECT_EQ(handle.error().code, ErrorCode::McpTooManyRequests);
            ++refused;
            QCoreApplication::processEvents();
            continue;
        }
        ++submitted;
        if (submitted % 7 == 0) {
            handle->cancel();
            ++cancelled;
        } else {
            expectedSum += value;
            handles.push_back(std::move(*handle));
        }
    }
    ASSERT_TRUE(runUntil([&] { return delivered == total - cancelled; }, 20s));
    ASSERT_TRUE(runUntil([&] { return offloader.running() == 0 && offloader.queued() == 0; }));
    runFor(20ms);
    EXPECT_EQ(delivered, total - cancelled);
    EXPECT_EQ(sum, expectedSum);
    EXPECT_GE(refused, 0);
}

} // namespace
} // namespace hio::mcp
