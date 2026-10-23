// libFuzzer target for the JSON-RPC envelope and both dialects (ADR-0152): every input is passed to
// parseMessage() and to McpServer::handle() (stdio and HTTP transport info, the fake-free registry
// below), so the dialect detection, header checks, schema validation and the method table see
// arbitrary JSON. Invariant: every message that is not a notification gets exactly one answer.

#include "hio/core/result.h"
#include "hio/mcp/activity.h"
#include "hio/mcp/consent.h"
#include "hio/mcp/json_rpc.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/reply_channel.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/schema.h"
#include "hio/mcp/server.h"
#include "hio/mcp/tool_call.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QCoreApplication>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QString>
#include <QtTypes>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <string_view>

namespace {

using namespace hio::mcp;

class AllowAll final : public IConsentBroker {
public:
    std::function<void()> request(ConsentRequest /*request*/,
                                  std::function<void(ConsentDecision)> done) override {
        done({});
        return {};
    }
    void clientGone(std::string_view /*clientKey*/) override {}
};

class NoActivity final : public IActivitySink {
public:
    void record(ActivityEntry /*entry*/) override {}
};

class CountingChannel final : public IReplyChannel {
public:
    int completions = 0;
    [[nodiscard]] bool canStream() const override { return true; }
    void startStream() override {}
    void notify(const QJsonObject& /*notification*/) override {}
    void complete(const QJsonObject& /*response*/, const ReplyMeta& /*meta*/) override {
        ++completions;
    }
    [[nodiscard]] bool isOpen() const override { return true; }
    void setClosedHandler(std::function<void(CloseReason)> /*handler*/) override {}
};

void mustSucceed(const hio::Status& status) {
    if (!status) {
        std::abort();
    }
}

Registry makeRegistry() {
    Registry registry;
    mustSucceed(registry.addTool(ToolSpec{
        .name = "echo",
        .title = {},
        .description = "Echo.",
        .inputSchema =
            Schema::object()
                .property(QStringLiteral("text"), Schema::string().maxLength(64), Required::Yes)
                .property(QStringLiteral("address"), Schema::address())
                .property(QStringLiteral("n"), Schema::integer().minimum(0).maximum(9))
                .property(QStringLiteral("list"), Schema::array(Schema::boolean()).maxItems(3))
                .json(),
        .outputSchema = {},
        .annotations = {},
        .consent = ConsentCategory::Flash,
        .marker = false,
        .start =
            [](const RequestContext& context, const QJsonObject& args, ToolEnv& env) {
                (void)context;
                (void)env;
                return ToolCall::completed(toolResult(args));
            },
        .validate = {}}));
    mustSucceed(registry.addResourceTemplate(ResourceTemplateSpec{
        .uriTemplate = "fuzz://{id}/x",
        .name = "x",
        .title = {},
        .description = {},
        .mimeType = "text/plain",
        .read = [](const RequestContext&, const QString& uri, const std::map<QString, QString>&) {
            return ResourceReadResult::text(uri, QStringLiteral("text/plain"), uri);
        }}));
    mustSucceed(registry.addPrompt(
        PromptSpec{.name = "p",
                   .title = {},
                   .description = {},
                   .arguments = {PromptArgument{.name = "a", .description = {}, .required = true}},
                   .get = [](const RequestContext&, const std::map<QString, QString>&)
                       -> hio::Result<PromptResult> { return PromptResult{}; }}));
    return registry;
}

struct Harness {
    Harness() { QLoggingCategory::setFilterRules(QStringLiteral("hio.*=false")); }

    int argc = 1;
    std::array<char, 5> name{'f', 'u', 'z', 'z', '\0'};
    std::array<char*, 2> argv{name.data(), nullptr};
    QCoreApplication app{argc, argv.data()};
    AllowAll consent;
    NoActivity activity;
    McpServer server{makeRegistry(), consent, activity, ServerOptions{}};
};

Harness& harness() {
    static Harness instance;
    return instance;
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): the libFuzzer entry point
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const QByteArray bytes = QByteArrayView(data, static_cast<qsizetype>(size)).toByteArray();
    auto parsed = parseMessage(bytes);
    // Notifications and responses get no answer; a subscriptions/listen stream stays open.
    const bool unanswered = parsed && (parsed->kind != MessageKind::Request ||
                                       parsed->method == QStringLiteral("subscriptions/listen"));

    Harness& h = harness();
    for (const TransportKind kind : {TransportKind::Stdio, TransportKind::Http}) {
        TransportInfo transport;
        transport.kind = kind;
        transport.connectionId = 1;
        if (kind == TransportKind::Http && parsed) {
            transport.protocolVersionHeader = "2026-07-28";
            transport.methodHeader = parsed->method.toStdString();
            transport.nameHeader =
                parsed->params.value(QStringLiteral("name")).toString().toStdString();
        }
        auto channel = std::make_shared<CountingChannel>();
        h.server.handle(bytes, channel, transport);
        if (unanswered ? channel->completions > 1 : channel->completions != 1) {
            std::abort();
        }
    }
    QCoreApplication::processEvents();
    return 0;
}
