#include "hio/core/error.h"
#include "output.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <gtest/gtest.h>

#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace {

using hio::cli::Output;
using hio::cli::OutputMode;

hio::Error sampleError(std::string detail) {
    return hio::Error{.code = hio::ErrorCode::IoFileNotFound,
                      .message = "The file was not found.",
                      .cause = "The path is wrong.",
                      .suggestion = "Check the file path.",
                      .detail = std::move(detail),
                      .wizard = std::nullopt};
}

TEST(Output, HumanErrorBlock) {
    std::ostringstream out;
    std::ostringstream err;
    Output output(OutputMode::Human, out, err, false);
    output.printError(sampleError("/tmp/a.hex"));
    EXPECT_EQ(out.str(), "");
    EXPECT_EQ(err.str(), "Error: The file was not found.\n"
                         "Cause: The path is wrong.\n"
                         "What to do: Check the file path.\n"
                         "Details: /tmp/a.hex\n");
}

TEST(Output, HumanErrorOmitsEmptyLines) {
    std::ostringstream out;
    std::ostringstream err;
    Output output(OutputMode::Human, out, err, false);
    hio::Error error = sampleError("");
    error.cause.clear();
    output.printError(error);
    EXPECT_EQ(err.str(), "Error: The file was not found.\n"
                         "What to do: Check the file path.\n");
}

TEST(Output, ColoredErrorLabel) {
    std::ostringstream out;
    std::ostringstream err;
    Output output(OutputMode::Human, out, err, true);
    output.printError(sampleError(""));
    EXPECT_TRUE(err.str().starts_with("\x1b[31mError:\x1b[0m The file was not found.\n"));
}

TEST(Output, JsonErrorGoesToStdout) {
    std::ostringstream out;
    std::ostringstream err;
    Output output(OutputMode::Json, out, err, true);
    output.printError(sampleError("/tmp/a.hex"));
    EXPECT_EQ(err.str(), "");
    EXPECT_EQ(out.str(),
              R"({"error":{"category":"io","cause":"The path is wrong.",)"
              R"("code":"io.file_not_found","detail":"/tmp/a.hex",)"
              R"("message":"The file was not found.","suggestion":"Check the file path.",)"
              R"("wizard":null},"ok":false,"schema":1})"
              "\n");
}

TEST(Output, HumanResult) {
    std::ostringstream out;
    std::ostringstream err;
    Output output(OutputMode::Human, out, err, false);
    output.printResult(QJsonValue(1), "hio 2.0.0");
    EXPECT_EQ(out.str(), "hio 2.0.0\n");
    EXPECT_EQ(err.str(), "");
}

TEST(Output, JsonResultAndEvents) {
    std::ostringstream out;
    std::ostringstream err;
    Output output(OutputMode::Json, out, err, false);
    QJsonObject data;
    data.insert(QStringLiteral("line"), QStringLiteral("hello"));
    output.printEvent(data, "hello");
    output.printEvent(data, "hello");
    output.printResult(QJsonValue(QJsonValue::Null), "ignored");
    EXPECT_EQ(out.str(), "{\"data\":{\"line\":\"hello\"},\"ok\":true,\"schema\":1}\n"
                         "{\"data\":{\"line\":\"hello\"},\"ok\":true,\"schema\":1}\n"
                         "{\"data\":null,\"ok\":true,\"schema\":1}\n");
}

} // namespace
