// Registry: registration rules and resource matching (plan/features/0013 "Protocol core").

#include "fake_tools.h"
#include "hio/core/error.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/schema.h"
#include "hio/mcp/tool_call.h"

#include <QJsonObject>
#include <QString>
#include <QStringLiteral>

#include <gtest/gtest.h>

#include <map>
#include <string_view>

namespace hio::mcp {
namespace {

ToolSpec simpleTool(std::string_view name) {
    return ToolSpec{
        .name = name,
        .title = {},
        .description = "d",
        .inputSchema = Schema::object().json(),
        .outputSchema = {},
        .annotations = {},
        .consent = {},
        .marker = false,
        .start = [](const RequestContext&, const QJsonObject&,
                    ToolEnv&) { return ToolCall::completed(toolResult(QJsonObject())); },
        .validate = {}};
}

TEST(Registry, RejectsInvalidAndDuplicateTools) {
    Registry registry;
    EXPECT_TRUE(registry.addTool(simpleTool("ok.name-1_x")).has_value());
    EXPECT_FALSE(registry.addTool(simpleTool("ok.name-1_x")).has_value());
    EXPECT_FALSE(registry.addTool(simpleTool("bad name")).has_value());
    EXPECT_FALSE(registry.addTool(simpleTool("")).has_value());
    ToolSpec noStart = simpleTool("nostart");
    noStart.start = nullptr;
    EXPECT_FALSE(registry.addTool(noStart).has_value());
    ToolSpec badSchema = simpleTool("bad_schema");
    badSchema.inputSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}};
    auto status = registry.addTool(badSchema);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, ErrorCode::InternalUnexpected);
    EXPECT_EQ(registry.tools().size(), 1U);
    EXPECT_NE(registry.findTool("ok.name-1_x"), nullptr);
    EXPECT_EQ(registry.findTool("nope"), nullptr);
}

TEST(Registry, MatchesStaticResourcesAndTemplates) {
    const Registry registry = test::makeFakeRegistry();
    const ResourceMatch none{.reader = nullptr, .variables = {}};
    const ResourceMatch staticMatch =
        registry.matchResource(QStringLiteral("test://static")).value_or(none);
    EXPECT_NE(staticMatch.reader, nullptr);
    EXPECT_TRUE(staticMatch.variables.empty());

    const ResourceMatch templated =
        registry.matchResource(QStringLiteral("test://item/jlink:801000001/status")).value_or(none);
    EXPECT_NE(templated.reader, nullptr);
    EXPECT_EQ(templated.variables, (std::map<QString, QString>{
                                       {QStringLiteral("id"), QStringLiteral("jlink:801000001")}}));

    EXPECT_FALSE(registry.matchResource(QStringLiteral("test://item/a/b/status")).has_value());
    EXPECT_FALSE(registry.matchResource(QStringLiteral("test://item//status")).has_value());
    EXPECT_FALSE(registry.matchResource(QStringLiteral("test://item/a/status/x")).has_value());
}

TEST(Registry, UriTemplateRules) {
    using Variables = std::map<QString, QString>;
    EXPECT_EQ(matchUriTemplate("a://{x}/{y}", QStringLiteral("a://1/2")).value_or(Variables{}),
              (Variables{{QStringLiteral("x"), QStringLiteral("1")},
                         {QStringLiteral("y"), QStringLiteral("2")}}));
    EXPECT_EQ(matchUriTemplate("a://{x}", QStringLiteral("a://%41")).value_or(Variables{}),
              (Variables{{QStringLiteral("x"), QStringLiteral("A")}}));
    EXPECT_FALSE(matchUriTemplate("a://{x", QStringLiteral("a://1")).has_value());
    EXPECT_FALSE(matchUriTemplate("a://x}", QStringLiteral("a://x}")).has_value());
    EXPECT_FALSE(matchUriTemplate("a://{}", QStringLiteral("a://1")).has_value());
    // Regex characters in the literal part are literal.
    EXPECT_FALSE(matchUriTemplate("a://.+/{x}", QStringLiteral("a://zz/1")).has_value());

    Registry registry;
    ResourceTemplateSpec spec{
        .uriTemplate = "a://none",
        .name = "n",
        .title = {},
        .description = {},
        .mimeType = {},
        .read = [](const RequestContext&, const QString&, const std::map<QString, QString>&) {
            return ResourceReadResult::notFound();
        }};
    EXPECT_FALSE(registry.addResourceTemplate(spec).has_value()); // no variable
}

} // namespace
} // namespace hio::mcp
