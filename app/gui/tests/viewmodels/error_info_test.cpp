// ErrorInfo: hio::Error for QML and the primary action per category (plan 0004, A4).

#include "hio/core/error.h"
#include "viewmodels/error_info.h"

#include <QString>

#include <gtest/gtest.h>

namespace {

using hio::ErrorCode;
using hio::gui::ErrorInfo;
namespace error_action = hio::gui::error_action;

TEST(ErrorInfo, DefaultIsInvalid) {
    const ErrorInfo info;
    EXPECT_FALSE(info.valid());
    EXPECT_TRUE(info.message().isEmpty());
    EXPECT_EQ(info.primaryAction(), error_action::Dismiss);
}

TEST(ErrorInfo, CarriesAllFields) {
    hio::Error error = hio::makeError(ErrorCode::TargetNoPower, "VTref = 0 mV");
    error.cause = "the battery is disconnected";
    error.suggestion = "connect power";
    error.wizard = hio::WizardId::ConnectDevice;
    const ErrorInfo info(error);
    EXPECT_TRUE(info.valid());
    EXPECT_EQ(info.code(), QStringLiteral("target.no_power"));
    EXPECT_EQ(info.category(), QStringLiteral("target"));
    EXPECT_EQ(info.message(), QString::fromStdString(error.message));
    EXPECT_EQ(info.cause(), QStringLiteral("the battery is disconnected"));
    EXPECT_EQ(info.suggestion(), QStringLiteral("connect power"));
    EXPECT_EQ(info.detail(), QStringLiteral("VTref = 0 mV"));
    EXPECT_EQ(info.wizard(), QStringLiteral("connect-device"));
    EXPECT_EQ(info.detailsText(), QStringLiteral("target.no_power\nVTref = 0 mV"));
}

TEST(ErrorInfo, NoWizardIsEmpty) {
    hio::Error error = hio::makeError(ErrorCode::IoReadFailed);
    error.wizard.reset();
    const ErrorInfo info(error);
    EXPECT_TRUE(info.wizard().isEmpty());
    EXPECT_EQ(info.detailsText(), QStringLiteral("io.read_failed"));
}

TEST(ErrorInfo, PrimaryActionFollowsTheCategory) {
    const auto action = [](ErrorCode code) {
        return ErrorInfo(hio::makeError(code)).primaryAction();
    };
    EXPECT_EQ(action(ErrorCode::ProbeNotFound), error_action::Reconnect);
    EXPECT_EQ(action(ErrorCode::TargetConnectFailed), error_action::Reconnect);
    EXPECT_EQ(action(ErrorCode::FirmwareVerifyFailed), error_action::ChooseFile);
    EXPECT_EQ(action(ErrorCode::DependencyJLinkNotFound), error_action::OpenSettings);
    EXPECT_EQ(action(ErrorCode::IoWriteFailed), error_action::Retry);
    EXPECT_EQ(action(ErrorCode::NetworkDownloadFailed), error_action::Retry);
    EXPECT_EQ(action(ErrorCode::Timeout), error_action::Retry);
    EXPECT_EQ(action(ErrorCode::InternalUnexpected), error_action::Retry);
    EXPECT_EQ(action(ErrorCode::UsageInvalidArgument), error_action::Dismiss);
    EXPECT_EQ(action(ErrorCode::Cancelled), error_action::Dismiss);
}

TEST(ErrorInfo, EveryCategoryHasAnAction) {
    for (const hio::ErrorInfo& entry : hio::errorTable) {
        const int action = ErrorInfo(hio::makeError(entry.code)).primaryAction();
        EXPECT_GE(action, error_action::Reconnect) << entry.id;
        EXPECT_LE(action, error_action::Dismiss) << entry.id;
    }
}

} // namespace
