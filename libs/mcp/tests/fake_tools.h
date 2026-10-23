#pragma once

// A small fake tool / resource / prompt set for the hio_mcp tests (golden exchanges, server,
// transports).
//   echo          sync; {text (<= 100 chars, required), count (1..10, default 1)} -> {text, count}
//   address       sync; {address (address pattern, required)} -> {address: "0x%08X"}
//   slow          async; {step_ms (0..10000, default 50), steps (0..1000, default 0)}: reports
//                 progress `steps` times every step_ms, then finishes {steps}
//   fail          sync; tool error mcp.device_not_open (detail "dev9")
//   flash_fake    consent Flash (asks with details), marker -> {flashed: true}, device "dev1"
//   no_consent    consent Flash in the spec but never asks (server safety net)
// Resources: test://static (text), template test://item/{id}/status ("missing" -> not found,
// "broken" -> error), prompt greet(name required, mood optional).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/mcp/consent.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/schema.h"
#include "hio/mcp/tool_call.h"

#include <QJsonObject>
#include <QString>
#include <QTimer>

#include <cstdlib>
#include <map>
#include <memory>

namespace hio::mcp::test {

class SlowCall final : public ToolCall {
public:
    SlowCall(int stepMs, int steps) : m_steps(steps) {
        m_timer.setInterval(stepMs);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            if (m_done < m_steps) {
                ++m_done;
                reportProgress(m_done, m_steps, QStringLiteral("step %1").arg(m_done));
                return;
            }
            m_timer.stop();
            finish(toolResult(QJsonObject{{QStringLiteral("steps"), m_steps}}));
        });
        m_timer.start();
    }

protected:
    void onCancel() override {
        m_timer.stop();
        ToolCall::onCancel();
    }

private:
    QTimer m_timer;
    int m_steps;
    int m_done = 0;
};

class FlashFakeCall final : public ToolCall {
public:
    FlashFakeCall(const RequestContext& context, ToolEnv& env) {
        setDeviceId("dev1");
        QJsonObject details;
        details.insert(QStringLiteral("file"), QStringLiteral("app.hex"));
        requestConsent(env.consentBroker(),
                       makeConsentRequest(context, ConsentCategory::Flash, "dev1", details),
                       [this](ConsentDecision decision) {
                           if (!decision) {
                               finish(std::unexpected(decision.error()));
                               return;
                           }
                           finish(toolResult(QJsonObject{{QStringLiteral("flashed"), true}}));
                       });
    }
};

// Registration of the fixed fake set cannot fail; a failure is a broken test setup.
inline void mustSucceed(const hio::Status& status) {
    if (!status) {
        std::abort();
    }
}

inline Registry makeFakeRegistry() {
    Registry registry;
    const QJsonObject anyOutput{{QStringLiteral("type"), QStringLiteral("object")}};

    mustSucceed(registry.addTool(ToolSpec{
        .name = "echo",
        .title = "Echo",
        .description = "Returns the text.",
        .inputSchema =
            Schema::object()
                .property(QStringLiteral("text"),
                          Schema::string(QStringLiteral("Text.")).maxLength(100), Required::Yes)
                .property(QStringLiteral("count"),
                          Schema::integer().minimum(1).maximum(10).defaultValue(1))
                .json(),
        .outputSchema = anyOutput,
        .annotations = ToolAnnotations{.readOnlyHint = true,
                                       .destructiveHint = false,
                                       .idempotentHint = true,
                                       .openWorldHint = false},
        .consent = {},
        .marker = false,
        .start = [](const RequestContext&, const QJsonObject& args,
                    ToolEnv&) { return ToolCall::completed(toolResult(args)); },
        .validate = {}}));

    mustSucceed(registry.addTool(ToolSpec{
        .name = "address",
        .title = {},
        .description = "Parses an address.",
        .inputSchema = Schema::object()
                           .property(QStringLiteral("address"), Schema::address(), Required::Yes)
                           .json(),
        .outputSchema = anyOutput,
        .annotations = {},
        .consent = {},
        .marker = false,
        .start =
            [](const RequestContext&, const QJsonObject& args, ToolEnv&) {
                const auto address = parseAddress(args.value(QStringLiteral("address")).toString());
                return ToolCall::completed(toolResult(
                    QJsonObject{{QStringLiteral("address"), formatAddress(address.value_or(0))}}));
            },
        .validate = {}}));

    mustSucceed(registry.addTool(ToolSpec{
        .name = "slow",
        .title = {},
        .description = "Finishes later.",
        .inputSchema = Schema::object()
                           .property(QStringLiteral("step_ms"),
                                     Schema::integer().minimum(0).maximum(10000).defaultValue(50))
                           .property(QStringLiteral("steps"),
                                     Schema::integer().minimum(0).maximum(1000).defaultValue(0))
                           .json(),
        .outputSchema = anyOutput,
        .annotations = {},
        .consent = {},
        .marker = false,
        .start =
            [](const RequestContext&, const QJsonObject& args, ToolEnv&) {
                return std::make_unique<SlowCall>(args.value(QStringLiteral("step_ms")).toInt(),
                                                  args.value(QStringLiteral("steps")).toInt());
            },
        .validate = {}}));

    mustSucceed(
        registry.addTool(ToolSpec{.name = "fail",
                                  .title = {},
                                  .description = "Fails.",
                                  .inputSchema = Schema::object().json(),
                                  .outputSchema = anyOutput,
                                  .annotations = {},
                                  .consent = {},
                                  .marker = false,
                                  .start =
                                      [](const RequestContext&, const QJsonObject&, ToolEnv&) {
                                          return ToolCall::completed(std::unexpected(
                                              makeError(ErrorCode::McpDeviceNotOpen, "dev9")));
                                      },
                                  .validate = {}}));

    mustSucceed(registry.addTool(ToolSpec{
        .name = "flash_fake",
        .title = {},
        .description = "Pretends to flash.",
        .inputSchema = Schema::object().json(),
        .outputSchema = anyOutput,
        .annotations = ToolAnnotations{.readOnlyHint = false,
                                       .destructiveHint = true,
                                       .idempotentHint = false,
                                       .openWorldHint = false},
        .consent = ConsentCategory::Flash,
        .marker = true,
        .start = [](const RequestContext& context, const QJsonObject&,
                    ToolEnv& env) { return std::make_unique<FlashFakeCall>(context, env); },
        .validate = {}}));

    mustSucceed(registry.addTool(ToolSpec{
        .name = "no_consent",
        .title = {},
        .description = "Forgets to ask.",
        .inputSchema = Schema::object().json(),
        .outputSchema = anyOutput,
        .annotations = {},
        .consent = ConsentCategory::Flash,
        .marker = false,
        .start =
            [](const RequestContext&, const QJsonObject&, ToolEnv&) {
                return ToolCall::completed(toolResult(QJsonObject{{QStringLiteral("ok"), true}}));
            },
        .validate = {}}));

    mustSucceed(registry.addResource(ResourceSpec{
        .uri = "test://static",
        .name = "static",
        .title = "Static",
        .description = "A static text.",
        .mimeType = "text/plain",
        .read = [](const RequestContext&, const QString& uri, const std::map<QString, QString>&) {
            return ResourceReadResult::text(uri, QStringLiteral("text/plain"),
                                            QStringLiteral("hello"));
        }}));

    mustSucceed(registry.addResourceTemplate(ResourceTemplateSpec{
        .uriTemplate = "test://item/{id}/status",
        .name = "item-status",
        .title = {},
        .description = "Item status.",
        .mimeType = "application/json",
        .read = [](const RequestContext&, const QString& uri,
                   const std::map<QString, QString>& variables) {
            const QString id = variables.at(QStringLiteral("id"));
            if (id == QStringLiteral("missing")) {
                return ResourceReadResult::notFound();
            }
            if (id == QStringLiteral("broken")) {
                return ResourceReadResult::failed(makeError(ErrorCode::McpDeviceNotOpen, "broken"));
            }
            return ResourceReadResult::text(uri, QStringLiteral("application/json"),
                                            QStringLiteral("{\"id\":\"%1\"}").arg(id));
        }}));

    registry.setResourceLister([](const RequestContext&) {
        return std::vector<ResourceInfo>{
            ResourceInfo{.uri = QStringLiteral("test://item/a/status"),
                         .name = QStringLiteral("a status"),
                         .title = {},
                         .description = {},
                         .mimeType = QStringLiteral("application/json")}};
    });

    mustSucceed(registry.addPrompt(PromptSpec{
        .name = "greet",
        .title = "Greet",
        .description = "Greets somebody.",
        .arguments = {PromptArgument{.name = "name", .description = "Who.", .required = true},
                      PromptArgument{.name = "mood", .description = "How.", .required = false}},
        .get = [](const RequestContext&,
                  const std::map<QString, QString>& arguments) -> hio::Result<PromptResult> {
            return PromptResult{
                .description = QStringLiteral("Greeting"),
                .messages = {PromptMessage{
                    .role = QStringLiteral("user"),
                    .text = QStringLiteral("Hello %1").arg(arguments.at(QStringLiteral("name")))}}};
        }}));
    return registry;
}

} // namespace hio::mcp::test
