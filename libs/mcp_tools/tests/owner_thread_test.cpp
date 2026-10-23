// Owner-thread stalls (plan/features/0013 "Tests: Owner-thread stalls", ADR-0097): with 32
// in-flight wait_for_state calls and 10 000 log lines/s from the fake J-Link, no owner-thread task
// may take more than 20 ms. A 5 ms QTimer probe measures the dispatch latency (how late each tick
// runs); the largest one bounds the longest task.

#include "hio/device/console_line.h"
#include "session_provider.h"
#include "tools_test_support.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTimer>
#include <QtCore/qnamespace.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

namespace hio::mcp::tools::test {
namespace {

using namespace std::chrono_literals;

// The bound applies to the normal (debug / release) builds. ASan/UBSan slow everything down by
// about 2-3x (bound 100 ms); under TSan (5-15x, and DeviceSession's own event batches dominate) the
// latency is only recorded, not checked.
#ifdef __has_feature
#if __has_feature(thread_sanitizer)
constexpr bool checkLatency = false;
constexpr auto allowedLatency = 1000ms;
#elif __has_feature(address_sanitizer)
constexpr bool checkLatency = true;
constexpr auto allowedLatency = 100ms;
#else
constexpr bool checkLatency = true;
constexpr auto allowedLatency = 20ms;
#endif
#else
constexpr bool checkLatency = true;
constexpr auto allowedLatency = 20ms;
#endif

TEST(OwnerThread, ThirtyTwoWaitsOnAFastStreamDoNotStall) {
    SessionProvider provider;
    const QJsonObject scenario{
        {QStringLiteral("probes"),
         QJsonArray{QJsonObject{
             {QStringLiteral("serialNumber"), 801000001},
             {QStringLiteral("rtt"),
              QJsonObject{
                  {QStringLiteral("up"),
                   QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("Terminal")},
                                          {QStringLiteral("size"), 1024}},
                              QJsonObject{{QStringLiteral("name"), QStringLiteral("Logger")},
                                          {QStringLiteral("size"), 65536}}}},
                  {QStringLiteral("stream"),
                   QJsonArray{
                       QJsonObject{{QStringLiteral("buffer"), 1},
                                   {QStringLiteral("linesPerSecond"), 10000},
                                   {QStringLiteral("text"),
                                    QStringLiteral("[00:00:01.000,000] <inf> perf: line")}}}}}}}}}};
    provider.scenario(QJsonDocument(scenario).toJson());
    Harness harness(provider);
    auto connect = harness.call(QStringLiteral("connect"), args({{"device", "jlink:801000001"}}));
    ASSERT_FALSE(connect.isError()) << connect.errorCode() << " " << connect.errorDetail();

    std::vector<PendingCall> waits;
    waits.reserve(32);
    for (int i = 0; i < 32; ++i) {
        waits.push_back(
            harness.start(QStringLiteral("wait_for_state"),
                          args({{"pattern", QStringLiteral("never (seen|matched) %1$").arg(i)},
                                {"timeout", 600},
                                {"source", "both"}})));
    }
    ASSERT_EQ(harness.server->activeCalls(), 32U);

    const auto log = provider.logOf("jlink:801000001");
    const std::uint64_t before = log->total(hio::device::Channel::Log);
    QElapsedTimer clock;
    clock.start();
    std::int64_t last = clock.elapsed();
    std::int64_t worst = 0;
    QTimer probe;
    probe.setTimerType(Qt::PreciseTimer);
    probe.setInterval(5ms);
    QObject::connect(&probe, &QTimer::timeout, [&] {
        const std::int64_t now = clock.elapsed();
        worst = std::max(worst, now - last - 5);
        last = now;
    });
    probe.start();
    runFor(3s);
    probe.stop();
    const std::uint64_t received = log->total(hio::device::Channel::Log) - before;

    RecordProperty("worst_dispatch_latency_ms", static_cast<int>(worst));
    RecordProperty("log_lines_received", static_cast<int>(received));
    EXPECT_GT(received, 25000U) << "the stream did not run";
    if (checkLatency) {
        EXPECT_LT(worst, allowedLatency.count()) << "an owner-thread task took " << worst << " ms";
    }
    for (const PendingCall& wait : waits) {
        EXPECT_FALSE(wait.done());
    }
    harness.server->stop();
    EXPECT_TRUE(runUntil([&] { return harness.server->activeCalls() == 0; }));
}

} // namespace
} // namespace hio::mcp::tools::test
