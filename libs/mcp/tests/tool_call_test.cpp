// ToolCall: finish exactly once, cancellation, consent helper, result helpers
// (plan/features/0013 "Protocol core").

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/mcp/consent.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/tool_call.h"
#include "mcp_test_support.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringLiteral>
#include <QtCore/qcborcommon.h>
#include <QtCore/qhashfunctions.h>

#include <gtest/gtest.h>

#include <expected>
#include <optional>
#include <utility>

namespace hio::mcp {
namespace {

using test::FakeConsentBroker;
using test::ManualToolCall;

// Placeholders that no code path produces (`timeout`).
hio::Result<ToolResult> notFinished() { return std::unexpected(makeError(ErrorCode::Timeout)); }
ConsentDecision notDecided() { return std::unexpected(makeError(ErrorCode::Timeout)); }

TEST(ToolCall, FinishesExactlyOnce) {
    ManualToolCall call;
    int finished = 0;
    QObject::connect(&call, &ToolCall::finished, [&finished](const auto&) { ++finished; });
    call.finish(toolResult(QJsonObject{{QStringLiteral("a"), 1}}));
    call.finish(std::unexpected(makeError(ErrorCode::Timeout)));
    call.cancel();
    EXPECT_EQ(finished, 1);
    EXPECT_TRUE(call.isFinished());
    EXPECT_TRUE(call.result().value_or(notFinished()).has_value());
    EXPECT_EQ(call.cancelCount, 0);
}

TEST(ToolCall, CancelIsIdempotentAndFinishesCancelled) {
    ManualToolCall call;
    call.cancel();
    call.cancel();
    EXPECT_EQ(call.cancelCount, 1);
    ASSERT_TRUE(call.isFinished());
    EXPECT_EQ(call.result().value_or(notFinished()).error().code, ErrorCode::Cancelled);
}

TEST(ToolCall, ProgressStopsAfterFinish) {
    ManualToolCall call;
    int progress = 0;
    QObject::connect(&call, &ToolCall::progress,
                     [&progress](double, auto, const auto&) { ++progress; });
    call.reportProgress(1);
    call.finish(toolResult({}));
    call.reportProgress(2);
    EXPECT_EQ(progress, 1);
}

TEST(ToolCall, ConsentGrantedAndDenied) {
    FakeConsentBroker broker;
    ManualToolCall call;
    ConsentDecision decision = notDecided();
    int answers = 0;
    RequestContext context;
    context.tool = "flash";
    context.clientKey = "k";
    call.requestConsent(broker, makeConsentRequest(context, ConsentCategory::Flash, "dev"),
                        [&](ConsentDecision d) {
                            decision = std::move(d);
                            ++answers;
                        });
    EXPECT_EQ(answers, 1);
    EXPECT_TRUE(decision.has_value());
    EXPECT_TRUE(call.grantedConsent().testFlag(ConsentCategory::Flash));
    ASSERT_EQ(broker.requests.size(), 1U);
    EXPECT_EQ(broker.requests[0].tool, "flash");
    EXPECT_EQ(broker.requests[0].clientKey, "k");
    EXPECT_EQ(broker.requests[0].deviceId, "dev");

    broker.mode = FakeConsentBroker::Mode::Deny;
    call.requestConsent(broker, makeConsentRequest(context, ConsentCategory::Uicr, "dev"),
                        [&](ConsentDecision d) {
                            decision = std::move(d);
                            ++answers;
                        });
    EXPECT_EQ(answers, 2);
    EXPECT_FALSE(decision.has_value());
    EXPECT_FALSE(call.grantedConsent().testFlag(ConsentCategory::Uicr));
}

TEST(ToolCall, CancelWithdrawsPendingConsent) {
    FakeConsentBroker broker;
    broker.mode = FakeConsentBroker::Mode::Manual;
    ManualToolCall call;
    bool called = false;
    call.requestConsent(broker, ConsentRequest{}, [&called](ConsentDecision) { called = true; });
    call.cancel();
    EXPECT_TRUE(broker.withdrawn(0));
    broker.decide(0, {});
    EXPECT_FALSE(called);
    EXPECT_TRUE(call.isFinished());
}

TEST(ToolCall, LateConsentAfterDestructionIsIgnored) {
    FakeConsentBroker broker;
    broker.mode = FakeConsentBroker::Mode::Manual;
    bool called = false;
    {
        ManualToolCall call;
        call.requestConsent(broker, ConsentRequest{},
                            [&called](ConsentDecision) { called = true; });
    }
    EXPECT_TRUE(broker.withdrawn(0));
    EXPECT_FALSE(called);
}

TEST(ToolCall, SecondConcurrentConsentRequestFails) {
    FakeConsentBroker broker;
    broker.mode = FakeConsentBroker::Mode::Manual;
    ManualToolCall call;
    call.requestConsent(broker, ConsentRequest{}, [](ConsentDecision) {});
    ConsentDecision second = notDecided();
    call.requestConsent(broker, ConsentRequest{}, [&second](ConsentDecision d) { second = d; });
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, ErrorCode::InternalUnexpected);
}

TEST(ToolCall, ResultHelpers) {
    const ToolResult ok =
        toolResult(QJsonObject{{QStringLiteral("b"), 2}, {QStringLiteral("a"), 1}});
    EXPECT_EQ(ok.text, QStringLiteral(R"({"a":1,"b":2})"));
    EXPECT_FALSE(ok.isError);

    Error error = makeError(ErrorCode::McpToolNotAllowed);
    error.message = "The tool erase is not allowed by this server.";
    error.suggestion = "Start the server with --allow erase (or --allow all).";
    const ToolResult failed = toolErrorResult(error);
    EXPECT_TRUE(failed.isError);
    EXPECT_EQ(failed.text, QStringLiteral("mcp.tool_not_allowed: The tool erase is not allowed by "
                                          "this server. \u2014 Start the server with --allow "
                                          "erase (or --allow all)."));
    EXPECT_EQ(failed.structured.value(QStringLiteral("error"))
                  .toObject()
                  .value(QStringLiteral("code"))
                  .toString(),
              QStringLiteral("mcp.tool_not_allowed"));
    error.suggestion.clear();
    EXPECT_FALSE(toolErrorResult(error).text.contains(QChar(0x2014)));

    const QJsonObject json = toolResultJson(ok);
    EXPECT_EQ(json.value(QStringLiteral("content")).toArray().size(), 1);
    EXPECT_FALSE(json.value(QStringLiteral("isError")).toBool(true));
}

TEST(Consent, CategoryIds) {
    EXPECT_EQ(consentCategoryId(ConsentCategory::MemoryWrite), "memory-write");
    EXPECT_EQ(consentCategoryFromId("uicr"), ConsentCategory::Uicr);
    EXPECT_FALSE(consentCategoryFromId("all").has_value());
    const auto ids = consentCategoryIds(ConsentCategory::Flash | ConsentCategory::Uicr);
    ASSERT_EQ(ids.size(), 2U);
    EXPECT_EQ(ids[0], "flash");
    EXPECT_EQ(ids[1], "uicr");
}

} // namespace
} // namespace hio::mcp
