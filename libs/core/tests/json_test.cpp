#include "hio/core/error.h"
#include "hio/core/json.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringLiteral>

#include <gtest/gtest.h>

#include <optional>
#include <string>

namespace {

hio::Error sampleError() {
    return hio::Error{.code = hio::ErrorCode::IoFileNotFound,
                      .message = "File not found.",
                      .cause = "",
                      .suggestion = "Check the path.",
                      .detail = "/tmp/x.hex",
                      .wizard = std::nullopt};
}

TEST(Json, ErrorObjectGolden) {
    EXPECT_EQ(hio::toCompactUtf8(hio::toJson(sampleError())),
              R"({"category":"io","cause":"","code":"io.file_not_found","detail":"/tmp/x.hex",)"
              R"("message":"File not found.","suggestion":"Check the path.","wizard":null})");
}

TEST(Json, WizardIsKebabCase) {
    hio::Error error = sampleError();
    error.wizard = hio::WizardId::ConnectDevice;
    EXPECT_EQ(hio::toJson(error).value(QStringLiteral("wizard")).toString(),
              QStringLiteral("connect-device"));
    error.wizard = hio::WizardId::FirstRunSetup;
    EXPECT_EQ(hio::toJson(error).value(QStringLiteral("wizard")).toString(),
              QStringLiteral("first-run-setup"));
    error.wizard = hio::WizardId::BlePairing;
    EXPECT_EQ(hio::toJson(error).value(QStringLiteral("wizard")).toString(),
              QStringLiteral("ble-pairing"));
    error.wizard = hio::WizardId::FlashFirmware;
    EXPECT_EQ(hio::toJson(error).value(QStringLiteral("wizard")).toString(),
              QStringLiteral("flash-firmware"));
    error.wizard = hio::WizardId::Troubleshooting;
    EXPECT_EQ(hio::toJson(error).value(QStringLiteral("wizard")).toString(),
              QStringLiteral("troubleshooting"));
}

TEST(Json, OkEnvelopeGolden) {
    QJsonObject data;
    data.insert(QStringLiteral("name"), QStringLiteral("hio"));
    data.insert(QStringLiteral("list"), QJsonArray{1, 2});
    EXPECT_EQ(hio::toCompactUtf8(hio::okEnvelope(data)),
              R"({"data":{"list":[1,2],"name":"hio"},"ok":true,"schema":1})");
}

TEST(Json, OkEnvelopeWithNullData) {
    EXPECT_EQ(hio::toCompactUtf8(hio::okEnvelope(QJsonValue(QJsonValue::Null))),
              R"({"data":null,"ok":true,"schema":1})");
}

TEST(Json, ErrorEnvelopeGolden) {
    EXPECT_EQ(hio::toCompactUtf8(hio::errorEnvelope(sampleError())),
              R"({"error":{"category":"io","cause":"","code":"io.file_not_found",)"
              R"("detail":"/tmp/x.hex","message":"File not found.","suggestion":"Check the path.",)"
              R"("wizard":null},"ok":false,"schema":1})");
}

TEST(Json, Utf8RoundTrip) {
    const std::string czech = "Čeština – žluťoučký kůň";
    const QString text = hio::toQString(czech);
    EXPECT_EQ(text.size(), 23);
    EXPECT_EQ(hio::toStdString(text), czech);

    hio::Error error = sampleError();
    error.message = czech;
    const std::string json = hio::toCompactUtf8(hio::toJson(error));
    EXPECT_NE(json.find(czech), std::string::npos);
}

TEST(Json, EmptyStringViewConverts) {
    EXPECT_TRUE(hio::toQString({}).isEmpty());
    EXPECT_TRUE(hio::toStdString(QString()).empty());
}

} // namespace
