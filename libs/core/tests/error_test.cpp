#include "hio/core/error.h"
#include "hio/core/result.h"

#include <gtest/gtest.h>

#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace {

using hio::ErrorCategory;
using hio::ErrorCode;

TEST(ErrorTable, IdsAreUniqueAndRoundTrip) {
    std::set<std::string_view> ids;
    for (const hio::ErrorInfo& info : hio::errorTable) {
        EXPECT_TRUE(ids.insert(info.id).second) << info.id;
        EXPECT_EQ(hio::idOf(info.code), info.id);
        EXPECT_EQ(hio::categoryOf(info.code), info.category);
        const auto parsed = hio::errorCodeFromId(info.id);
        EXPECT_EQ(parsed, std::optional<hio::ErrorCode>(info.code)) << info.id;
    }
}

TEST(ErrorTable, KnownIdsAndCategories) {
    EXPECT_EQ(hio::idOf(ErrorCode::IoFileNotFound), "io.file_not_found");
    EXPECT_EQ(hio::categoryOf(ErrorCode::IoFileNotFound), ErrorCategory::Io);
    EXPECT_EQ(hio::categoryOf(ErrorCode::SettingsNewerSchema), ErrorCategory::Io);
    EXPECT_EQ(hio::categoryOf(ErrorCode::Cancelled), ErrorCategory::Cancelled);
    EXPECT_EQ(hio::categoryOf(ErrorCode::UsageInvalidArgument), ErrorCategory::Usage);
    EXPECT_EQ(hio::categoryOf(ErrorCode::InternalQueueFull), ErrorCategory::Internal);
    EXPECT_EQ(hio::categoryOf(ErrorCode::Timeout), ErrorCategory::Timeout);
}

TEST(ErrorTable, UnknownIdIsRejected) {
    EXPECT_FALSE(hio::errorCodeFromId("").has_value());
    EXPECT_FALSE(hio::errorCodeFromId("io").has_value());
    EXPECT_FALSE(hio::errorCodeFromId("IO.FILE_NOT_FOUND").has_value());
    EXPECT_FALSE(hio::errorCodeFromId("probe.not_found").has_value());
}

TEST(ErrorCategoryId, AllCategoriesHaveStableIds) {
    EXPECT_EQ(hio::categoryId(ErrorCategory::Usage), "usage");
    EXPECT_EQ(hio::categoryId(ErrorCategory::Io), "io");
    EXPECT_EQ(hio::categoryId(ErrorCategory::Dependency), "dependency");
    EXPECT_EQ(hio::categoryId(ErrorCategory::Probe), "probe");
    EXPECT_EQ(hio::categoryId(ErrorCategory::Target), "target");
    EXPECT_EQ(hio::categoryId(ErrorCategory::Firmware), "firmware");
    EXPECT_EQ(hio::categoryId(ErrorCategory::Network), "network");
    EXPECT_EQ(hio::categoryId(ErrorCategory::Timeout), "timeout");
    EXPECT_EQ(hio::categoryId(ErrorCategory::Cancelled), "cancelled");
    EXPECT_EQ(hio::categoryId(ErrorCategory::Internal), "internal");
}

TEST(MakeError, EveryCodeHasDefaultTexts) {
    for (const hio::ErrorInfo& info : hio::errorTable) {
        const hio::Error error = hio::makeError(info.code, "detail");
        EXPECT_EQ(error.code, info.code);
        EXPECT_FALSE(error.message.empty()) << info.id;
        EXPECT_FALSE(error.cause.empty()) << info.id;
        EXPECT_FALSE(error.suggestion.empty()) << info.id;
        EXPECT_EQ(error.detail, "detail");
        EXPECT_FALSE(error.wizard.has_value());
    }
    EXPECT_EQ(hio::makeError(ErrorCode::Cancelled).message, "Cancelled by user.");
    EXPECT_EQ(hio::makeError(ErrorCode::InternalUnexpected).suggestion,
              "Please report it with the log file (see Details).");
}

TEST(Fail, FullFormKeepsAllTexts) {
    const hio::Status status =
        hio::fail(ErrorCode::IoReadFailed, "message", "cause", "suggestion", "detail");
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, ErrorCode::IoReadFailed);
    EXPECT_EQ(status.error().message, "message");
    EXPECT_EQ(status.error().cause, "cause");
    EXPECT_EQ(status.error().suggestion, "suggestion");
    EXPECT_EQ(status.error().detail, "detail");
}

TEST(Fail, DefaultTextsForm) {
    const hio::Result<int> result = hio::fail(ErrorCode::Timeout);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::Timeout);
    EXPECT_EQ(result.error().message, hio::makeError(ErrorCode::Timeout).message);
    EXPECT_TRUE(result.error().detail.empty());
}

} // namespace
