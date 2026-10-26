// Schema builders, the supported subset and argument validation (plan/features/0013 "Argument
// validation").

#include "hio/mcp/schema.h"

#include <QCborValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QStringLiteral>

#include <gtest/gtest.h>

#include <array>

namespace hio::mcp {
namespace {

QJsonObject json(const char* text) { return QJsonDocument::fromJson(QByteArray(text)).object(); }

QJsonObject toolSchema() {
    return Schema::object()
        .property(QStringLiteral("address"), Schema::address(QStringLiteral("Start.")),
                  Required::Yes)
        .property(QStringLiteral("length"),
                  Schema::integer().minimum(1).maximum(65536).defaultValue(64))
        .property(QStringLiteral("width"),
                  Schema::string().enumValues({QStringLiteral("8"), QStringLiteral("16")}))
        .property(QStringLiteral("ratio"), Schema::number().minimum(0.5).maximum(60))
        .property(QStringLiteral("flag"), Schema::boolean())
        .property(QStringLiteral("lines"), Schema::array(Schema::string().maxLength(3)).maxItems(2))
        .property(
            QStringLiteral("nested"),
            Schema::object().property(QStringLiteral("a/b~c"), Schema::integer(), Required::Yes))
        .json();
}

TEST(Schema, BuildsTheExpectedJson) {
    const QJsonObject schema =
        Schema::object()
            .property(QStringLiteral("n"), Schema::integer(QStringLiteral("N.")).minimum(1),
                      Required::Yes)
            .json();
    EXPECT_EQ(schema, json(R"({"type":"object","additionalProperties":false,"required":["n"],
        "properties":{"n":{"type":"integer","description":"N.","minimum":1}}})"));
    EXPECT_TRUE(checkSchemaSubset(toolSchema()).has_value());
}

TEST(Schema, ValidArgumentsGetDefaults) {
    auto result = validateArguments(toolSchema(), json(R"({"address":"0x20000000"})"));
    ASSERT_TRUE(result.has_value()) << result.error().message.toStdString();
    EXPECT_EQ(result->value(QStringLiteral("length")).toInt(), 64);
    EXPECT_FALSE(result->contains(QStringLiteral("width")));

    auto full = validateArguments(toolSchema(),
                                  json(R"({"address":"123","length":65536,"width":"16","ratio":0.5,
            "flag":true,"lines":["a","abc"],"nested":{"a/b~c":3}})"));
    EXPECT_TRUE(full.has_value());
}

TEST(Schema, ReportsTheFirstOffendingField) {
    struct Case {
        const char* args;
        const char* pointer;
    };
    const std::array cases{
        Case{.args = R"({})", .pointer = "/arguments/address"},
        Case{.args = R"({"address":"0x"})", .pointer = "/arguments/address"},
        Case{.args = R"({"address":"0x123456789"})", .pointer = "/arguments/address"},
        Case{.args = R"({"address":"12345678901"})", .pointer = "/arguments/address"},
        Case{.args = R"({"address":1})", .pointer = "/arguments/address"},
        Case{.args = R"({"address":"1","length":0})", .pointer = "/arguments/length"},
        Case{.args = R"({"address":"1","length":65537})", .pointer = "/arguments/length"},
        Case{.args = R"({"address":"1","length":1.5})", .pointer = "/arguments/length"},
        Case{.args = R"({"address":"1","length":"1"})", .pointer = "/arguments/length"},
        Case{.args = R"({"address":"1","width":"32"})", .pointer = "/arguments/width"},
        Case{.args = R"({"address":"1","ratio":0.1})", .pointer = "/arguments/ratio"},
        Case{.args = R"({"address":"1","flag":1})", .pointer = "/arguments/flag"},
        Case{.args = R"({"address":"1","lines":["a","b","c"]})", .pointer = "/arguments/lines"},
        Case{.args = R"({"address":"1","lines":["abcd"]})", .pointer = "/arguments/lines/0"},
        Case{.args = R"({"address":"1","nested":{}})", .pointer = "/arguments/nested/a~1b~0c"},
        Case{.args = R"({"address":"1","nested":{"a/b~c":1,"x":2}})",
             .pointer = "/arguments/nested/x"},
        Case{.args = R"({"address":"1","zzz":1})", .pointer = "/arguments/zzz"},
    };
    for (const Case& c : cases) {
        auto result = validateArguments(toolSchema(), json(c.args));
        ASSERT_FALSE(result.has_value()) << c.args;
        EXPECT_EQ(result.error().pointer.toStdString(), c.pointer) << c.args;
        EXPECT_FALSE(result.error().message.isEmpty());
    }
}

TEST(Schema, RejectsSchemasOutsideTheSubset) {
    EXPECT_FALSE(checkSchemaSubset(json(R"({"type":"string"})")).has_value());
    EXPECT_FALSE(
        checkSchemaSubset(json(R"({"type":"object"})")).has_value()); // no additionalProperties
    EXPECT_FALSE(
        checkSchemaSubset(
            json(
                R"({"type":"object","additionalProperties":false,"properties":{"a":{"oneOf":[]}}})"))
            .has_value());
    EXPECT_FALSE(
        checkSchemaSubset(
            json(
                R"({"type":"object","additionalProperties":false,"properties":{"a":{"type":"null"}}})"))
            .has_value());
    EXPECT_FALSE(
        checkSchemaSubset(
            json(
                R"({"type":"object","additionalProperties":false,"required":["b"],"properties":{}})"))
            .has_value());
    EXPECT_FALSE(
        checkSchemaSubset(
            json(
                R"({"type":"object","additionalProperties":false,"properties":{"a":{"type":"string","pattern":"("}}})"))
            .has_value());
}

TEST(Schema, ParsesAndFormatsAddresses) {
    EXPECT_EQ(parseAddress(u"0x20000000"), 0x20000000U);
    EXPECT_EQ(parseAddress(u"0xffffffff"), 0xFFFFFFFFU);
    EXPECT_EQ(parseAddress(u"4294967295"), 0xFFFFFFFFU);
    EXPECT_EQ(parseAddress(u"0"), 0U);
    EXPECT_FALSE(parseAddress(u"4294967296").has_value());
    EXPECT_FALSE(parseAddress(u"0x100000000").has_value());
    EXPECT_FALSE(parseAddress(u"0X10").has_value());
    EXPECT_FALSE(parseAddress(u" 1").has_value());
    EXPECT_FALSE(parseAddress(u"").has_value());
    EXPECT_EQ(formatAddress(0x2000ABCD), QStringLiteral("0x2000ABCD"));
    EXPECT_EQ(formatAddress(0), QStringLiteral("0x00000000"));
}

} // namespace
} // namespace hio::mcp
