// CheckRunner (plan/features/0007, A1): order, dependencies, timeout, stop token, callback.

#include "hio/core/error.h"
#include "hio/diag/check.h"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace {

using hio::ErrorCode;
using hio::diag::Check;
using hio::diag::CheckResult;
using hio::diag::CheckRunner;
using hio::diag::CheckStatus;
using namespace std::chrono_literals;

Check makeCheck(std::string id, CheckStatus status, std::vector<std::string> dependsOn = {}) {
    Check check{.id = std::move(id),
                .titleSourceText = "Title",
                .timeout = 1000ms,
                .dependsOn = std::move(dependsOn),
                .run = {}};
    check.run = [status, check](const std::stop_token&) {
        if (status == CheckStatus::Ok) {
            return hio::diag::okResult(check, "fine");
        }
        return hio::diag::problemResult(check, status, hio::makeError(ErrorCode::IoReadFailed));
    };
    return check;
}

TEST(CheckRunner, RunsInOrderAndReportsRunningFirst) {
    const std::vector checks{makeCheck("a", CheckStatus::Ok), makeCheck("b", CheckStatus::Warning),
                             makeCheck("c", CheckStatus::Failed)};
    std::vector<std::string> events;
    const auto results = CheckRunner::run(checks, {}, [&](const CheckResult& result) {
        events.push_back(result.id + ":" + std::string(hio::diag::checkStatusId(result.status)));
    });
    ASSERT_EQ(results.size(), 3U);
    EXPECT_EQ(results[0].status, CheckStatus::Ok);
    EXPECT_EQ(results[0].result, "fine");
    EXPECT_EQ(results[0].title, "Title");
    EXPECT_EQ(results[1].status, CheckStatus::Warning);
    EXPECT_EQ(results[2].status, CheckStatus::Failed);
    ASSERT_TRUE(results[2].error.has_value());
    EXPECT_EQ(events, (std::vector<std::string>{"a:running", "a:ok", "b:running", "b:warning",
                                                "c:running", "c:failed"}));
}

TEST(CheckRunner, DependencyThatDidNotPassSkips) {
    const std::vector checks{makeCheck("lib", CheckStatus::Failed),
                             makeCheck("probes", CheckStatus::Ok, {"lib"}),
                             makeCheck("warn", CheckStatus::Warning),
                             makeCheck("after-warning", CheckStatus::Ok, {"warn"}),
                             makeCheck("unknown-dep", CheckStatus::Ok, {"nope"})};
    const auto results = CheckRunner::run(checks, {});
    ASSERT_EQ(results.size(), 5U);
    EXPECT_EQ(results[1].status, CheckStatus::Skipped);
    EXPECT_FALSE(results[1].error.has_value());
    EXPECT_EQ(results[3].status, CheckStatus::Ok);
    EXPECT_EQ(results[4].status, CheckStatus::Skipped);
}

TEST(CheckRunner, TimeoutStopsTheCheckAndWarns) {
    Check slow{
        .id = "slow", .titleSourceText = "Slow", .timeout = 100ms, .dependsOn = {}, .run = {}};
    slow.run = [slow](const std::stop_token& stop) {
        std::mutex mutex;
        std::condition_variable_any wake;
        std::unique_lock lock(mutex);
        wake.wait_for(lock, stop, 10s, [] { return false; });
        return hio::diag::okResult(slow, "late");
    };
    const auto started = std::chrono::steady_clock::now();
    const auto results =
        CheckRunner::run(std::vector{slow, makeCheck("next", CheckStatus::Ok)}, {});
    EXPECT_LT(std::chrono::steady_clock::now() - started, 5s);
    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].status, CheckStatus::Warning);
    ASSERT_TRUE(results[0].error.has_value());
    EXPECT_EQ(results[0].error.value_or(hio::Error{}).code, ErrorCode::DiagCheckTimeout);
    EXPECT_NE(results[0].error.value_or(hio::Error{}).message.find("Slow"), std::string::npos);
    EXPECT_EQ(results[1].status, CheckStatus::Ok);
}

TEST(CheckRunner, StopRequestSkipsTheRest) {
    std::stop_source stop;
    Check first{
        .id = "first", .titleSourceText = "First", .timeout = 1000ms, .dependsOn = {}, .run = {}};
    first.run = [first, &stop](const std::stop_token&) {
        stop.request_stop();
        return hio::diag::okResult(first, "done");
    };
    const auto results = CheckRunner::run(std::vector{first, makeCheck("second", CheckStatus::Ok)},
                                          stop.get_token());
    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].status, CheckStatus::Ok);
    EXPECT_EQ(results[1].status, CheckStatus::Skipped);
}

TEST(CheckRunner, AtMostSixteenChecksRun) {
    std::vector<Check> checks;
    checks.reserve(18);
    for (int i = 0; i < 18; ++i) {
        checks.push_back(makeCheck("c" + std::to_string(i), CheckStatus::Ok));
    }
    const auto results = CheckRunner::run(checks, {});
    ASSERT_EQ(results.size(), 18U);
    EXPECT_EQ(results[15].status, CheckStatus::Ok);
    EXPECT_EQ(results[16].status, CheckStatus::Skipped);
    EXPECT_EQ(results[17].status, CheckStatus::Skipped);
}

TEST(CheckRunner, FormatsAlignedLines) {
    const std::vector<CheckResult> results{
        {.id = "a", .status = CheckStatus::Ok, .title = "Short", .result = "x", .error = {}},
        {.id = "b",
         .status = CheckStatus::Failed,
         .title = "Longer title",
         .result = "y",
         .error = {}}};
    EXPECT_EQ(hio::diag::formatCheckLines(results), "✓ Short         x\n✗ Longer title  y\n");
}

} // namespace
