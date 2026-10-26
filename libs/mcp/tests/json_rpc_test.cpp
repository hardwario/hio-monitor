// JSON-RPC envelope parsing and building (plan/features/0013 "Protocol core").

#include "hio/mcp/json_rpc.h"
#include "hio/mcp/protocol.h"

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringLiteral>

#include <gtest/gtest.h>

#include <array>

namespace hio::mcp {
namespace {

TEST(JsonRpc, ParsesRequestNotificationAndResponse) {
    auto request =
        parseMessage(R"({"jsonrpc":"2.0","id":7,"method":"tools/list","params":{"a":1}})");
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->kind, MessageKind::Request);
    EXPECT_EQ(request->id, QJsonValue(7));
    EXPECT_EQ(request->method, QStringLiteral("tools/list"));
    EXPECT_EQ(request->params.value(QStringLiteral("a")).toInt(), 1);

    auto notification = parseMessage(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
    ASSERT_TRUE(notification.has_value());
    EXPECT_EQ(notification->kind, MessageKind::Notification);
    EXPECT_TRUE(notification->id.isUndefined());

    auto response = parseMessage(R"({"jsonrpc":"2.0","id":"x","result":{}})");
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->kind, MessageKind::Response);
}

TEST(JsonRpc, RejectsMalformedMessages) {
    struct Case {
        const char* text;
        int code;
        bool idEchoed;
    };
    const std::array cases{
        Case{.text = "", .code = protocol::errc::parseError, .idEchoed = false},
        Case{.text = "{", .code = protocol::errc::parseError, .idEchoed = false},
        Case{.text = "[]", .code = protocol::errc::invalidRequest, .idEchoed = false},
        Case{.text = R"([{"jsonrpc":"2.0","id":1,"method":"ping"}])",
             .code = protocol::errc::invalidRequest,
             .idEchoed = false},
        Case{.text = "42", .code = protocol::errc::parseError, .idEchoed = false},
        Case{.text = R"({"id":1,"method":"ping"})",
             .code = protocol::errc::invalidRequest,
             .idEchoed = true},
        Case{.text = R"({"jsonrpc":"2.0","id":null,"method":"ping"})",
             .code = protocol::errc::invalidRequest,
             .idEchoed = false},
        Case{.text = R"({"jsonrpc":"2.0","id":{},"method":"ping"})",
             .code = protocol::errc::invalidRequest,
             .idEchoed = false},
        Case{.text = R"({"jsonrpc":"2.0","id":1e300,"method":"ping"})",
             .code = protocol::errc::invalidRequest,
             .idEchoed = false},
        Case{.text = R"({"jsonrpc":"2.0","id":1,"method":5})",
             .code = protocol::errc::invalidRequest,
             .idEchoed = true},
        Case{.text = R"({"jsonrpc":"2.0","id":1,"method":""})",
             .code = protocol::errc::invalidRequest,
             .idEchoed = true},
        Case{.text = R"({"jsonrpc":"2.0","id":1})",
             .code = protocol::errc::invalidRequest,
             .idEchoed = true},
        Case{.text = R"({"jsonrpc":"2.0","id":1,"method":"x","params":"p"})",
             .code = protocol::errc::invalidParams,
             .idEchoed = true},
    };
    for (const Case& c : cases) {
        auto parsed = parseMessage(QByteArray(c.text));
        ASSERT_FALSE(parsed.has_value()) << c.text;
        EXPECT_EQ(parsed.error().error.code, c.code) << c.text;
        EXPECT_EQ(!parsed.error().id.isUndefined(), c.idEchoed) << c.text;
    }
}

TEST(JsonRpc, IdsAreStringsOrSafeIntegers) {
    EXPECT_TRUE(isValidRequestId(QJsonValue(QStringLiteral(""))));
    EXPECT_TRUE(isValidRequestId(QJsonValue(0)));
    EXPECT_TRUE(isValidRequestId(QJsonValue(-5)));
    EXPECT_TRUE(isValidRequestId(QJsonValue(9007199254740991.0)));
    EXPECT_FALSE(isValidRequestId(QJsonValue(9007199254740992.0 * 2)));
    EXPECT_FALSE(isValidRequestId(QJsonValue(1.5)));
    EXPECT_FALSE(isValidRequestId(QJsonValue(true)));
    EXPECT_FALSE(isValidRequestId(QJsonValue(QJsonValue::Null)));
}

TEST(JsonRpc, BuildsResponses) {
    EXPECT_EQ(serialize(resultResponse(QJsonValue(3), QJsonObject())),
              QByteArray(R"({"id":3,"jsonrpc":"2.0","result":{}})"));
    EXPECT_EQ(serialize(errorResponse(QJsonValue(QStringLiteral("a")),
                                      RpcError{.code = -32601, .message = QStringLiteral("m")})),
              QByteArray(R"({"error":{"code":-32601,"message":"m"},"id":"a","jsonrpc":"2.0"})"));
    // No id member for an unknown id (the MCP schemas allow no null id).
    EXPECT_EQ(serialize(errorResponse(
                  QJsonValue(QJsonValue::Null),
                  RpcError{.code = -32700, .message = QStringLiteral("p"), .data = QJsonValue(1)})),
              QByteArray(R"({"error":{"code":-32700,"data":1,"message":"p"},"jsonrpc":"2.0"})"));
    EXPECT_EQ(serialize(notificationMessage(QStringLiteral("n"), QJsonObject())),
              QByteArray(R"({"jsonrpc":"2.0","method":"n","params":{}})"));
}

TEST(JsonRpc, LargeIntegerIdIsEchoedExactly) {
    auto parsed = parseMessage(R"({"jsonrpc":"2.0","id":9007199254740991,"method":"ping"})");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(serialize(resultResponse(parsed->id, QJsonObject())),
              QByteArray(R"({"id":9007199254740991,"jsonrpc":"2.0","result":{}})"));
}

} // namespace
} // namespace hio::mcp
