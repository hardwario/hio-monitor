#include "hwio/core/error.h"
#include "hwio/core/json.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringLiteral>

#include <gtest/gtest.h>

#include <optional>
#include <string>

namespace {

hwio::Error sampleError() {
    return hwio::Error{.code = hwio::ErrorCode::IoFileNotFound,
                       .message = "File not found.",
                       .cause = "",
                       .suggestion = "Check the path.",
                       .detail = "/tmp/x.hex",
                       .wizard = std::nullopt};
}

TEST(Json, ErrorObjectGolden) {
    EXPECT_EQ(hwio::toCompactUtf8(hwio::toJson(sampleError())),
              R"({"category":"io","cause":"","code":"io.file_not_found","detail":"/tmp/x.hex",)"
              R"("message":"File not found.","suggestion":"Check the path.","wizard":null})");
}

TEST(Json, WizardIsKebabCase) {
    hwio::Error error = sampleError();
    error.wizard = hwio::WizardId::ConnectDevice;
    EXPECT_EQ(hwio::toJson(error).value(QStringLiteral("wizard")).toString(),
              QStringLiteral("connect-device"));
    error.wizard = hwio::WizardId::FirstRunSetup;
    EXPECT_EQ(hwio::toJson(error).value(QStringLiteral("wizard")).toString(),
              QStringLiteral("first-run-setup"));
    error.wizard = hwio::WizardId::BlePairing;
    EXPECT_EQ(hwio::toJson(error).value(QStringLiteral("wizard")).toString(),
              QStringLiteral("ble-pairing"));
    error.wizard = hwio::WizardId::FlashFirmware;
    EXPECT_EQ(hwio::toJson(error).value(QStringLiteral("wizard")).toString(),
              QStringLiteral("flash-firmware"));
    error.wizard = hwio::WizardId::Troubleshooting;
    EXPECT_EQ(hwio::toJson(error).value(QStringLiteral("wizard")).toString(),
              QStringLiteral("troubleshooting"));
}

TEST(Json, OkEnvelopeGolden) {
    QJsonObject data;
    data.insert(QStringLiteral("name"), QStringLiteral("hwio"));
    data.insert(QStringLiteral("list"), QJsonArray{1, 2});
    EXPECT_EQ(hwio::toCompactUtf8(hwio::okEnvelope(data)),
              R"({"data":{"list":[1,2],"name":"hwio"},"ok":true,"schema":1})");
}

TEST(Json, OkEnvelopeWithNullData) {
    EXPECT_EQ(hwio::toCompactUtf8(hwio::okEnvelope(QJsonValue(QJsonValue::Null))),
              R"({"data":null,"ok":true,"schema":1})");
}

TEST(Json, ErrorEnvelopeGolden) {
    EXPECT_EQ(hwio::toCompactUtf8(hwio::errorEnvelope(sampleError())),
              R"({"error":{"category":"io","cause":"","code":"io.file_not_found",)"
              R"("detail":"/tmp/x.hex","message":"File not found.","suggestion":"Check the path.",)"
              R"("wizard":null},"ok":false,"schema":1})");
}

TEST(Json, Utf8RoundTrip) {
    const std::string czech = "Čeština – žluťoučký kůň";
    const QString text = hwio::toQString(czech);
    EXPECT_EQ(text.size(), 23);
    EXPECT_EQ(hwio::toStdString(text), czech);

    hwio::Error error = sampleError();
    error.message = czech;
    const std::string json = hwio::toCompactUtf8(hwio::toJson(error));
    EXPECT_NE(json.find(czech), std::string::npos);
}

TEST(Json, EmptyStringViewConverts) {
    EXPECT_TRUE(hwio::toQString({}).isEmpty());
    EXPECT_TRUE(hwio::toStdString(QString()).empty());
}

} // namespace
