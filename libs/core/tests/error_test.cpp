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
    EXPECT_FALSE(hio::errorCodeFromId("probe.no_such_error").has_value());
    EXPECT_EQ(hio::errorCodeFromId("probe.not_found"), ErrorCode::ProbeNotFound);
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
    }
    EXPECT_FALSE(hio::makeError(ErrorCode::IoReadFailed).wizard.has_value());
    EXPECT_EQ(hio::makeError(ErrorCode::Cancelled).message, "Cancelled by user.");
    EXPECT_EQ(hio::makeError(ErrorCode::InternalUnexpected).suggestion,
              "Please report it with the log file (see Details).");
}

// M1 codes (plan/features/0002-m1-jlink-rtt.md): categories and wizards.
TEST(MakeError, JLinkCodesHaveCategoriesAndWizards) {
    EXPECT_EQ(hio::categoryOf(ErrorCode::DependencyJLinkNotFound), ErrorCategory::Dependency);
    EXPECT_EQ(hio::categoryOf(ErrorCode::ProbeInUseByOtherProcess), ErrorCategory::Probe);
    EXPECT_EQ(hio::categoryOf(ErrorCode::TargetNoPower), ErrorCategory::Target);
    EXPECT_EQ(hio::categoryOf(ErrorCode::InternalSessionLimit), ErrorCategory::Internal);
    EXPECT_EQ(hio::categoryOf(ErrorCode::UsageConfirmationRequired), ErrorCategory::Usage);
    EXPECT_EQ(hio::idOf(ErrorCode::TargetRttBufferNotFound), "target.rtt_buffer_not_found");
    EXPECT_EQ(hio::makeError(ErrorCode::DependencyJLinkTooOld).wizard,
              hio::WizardId::FirstRunSetup);
    EXPECT_EQ(hio::makeError(ErrorCode::TargetNoPower).wizard, hio::WizardId::ConnectDevice);
    EXPECT_EQ(hio::makeError(ErrorCode::TargetRttReadFailed).wizard,
              hio::WizardId::Troubleshooting);
    EXPECT_FALSE(hio::makeError(ErrorCode::ProbeInUseByOtherProcess).wizard.has_value());
}

// Review fix 5 (2026-10-09): RTT write failures have their own code.
TEST(MakeError, RttWriteFailedCode) {
    EXPECT_EQ(hio::idOf(ErrorCode::TargetRttWriteFailed), "target.rtt_write_failed");
    EXPECT_EQ(hio::categoryOf(ErrorCode::TargetRttWriteFailed), ErrorCategory::Target);
    EXPECT_EQ(hio::errorCodeFromId("target.rtt_write_failed"), ErrorCode::TargetRttWriteFailed);
    const hio::Error error = hio::makeError(ErrorCode::TargetRttWriteFailed);
    EXPECT_EQ(error.message, "Writing to the device failed.");
    EXPECT_EQ(error.cause, "A cable was disconnected or the device was reset.");
    EXPECT_EQ(error.suggestion,
              "Check the cables; the console reconnects automatically if enabled.");
    EXPECT_EQ(error.wizard, hio::WizardId::Troubleshooting);
}

// M1 review R11 / R18 (2026-10-10).
TEST(MakeError, FirmwareUpdateFailedAndNotConnectedCodes) {
    EXPECT_EQ(hio::idOf(ErrorCode::ProbeFirmwareUpdateFailed), "probe.firmware_update_failed");
    EXPECT_EQ(hio::categoryOf(ErrorCode::ProbeFirmwareUpdateFailed), ErrorCategory::Probe);
    EXPECT_EQ(hio::makeError(ErrorCode::ProbeFirmwareUpdateFailed).wizard,
              hio::WizardId::Troubleshooting);
    EXPECT_EQ(hio::idOf(ErrorCode::TargetNotConnected), "target.not_connected");
    EXPECT_EQ(hio::categoryOf(ErrorCode::TargetNotConnected), ErrorCategory::Target);
    const hio::Error error = hio::makeError(ErrorCode::TargetNotConnected);
    EXPECT_EQ(error.message, "The device is not connected.");
    EXPECT_EQ(error.cause, "The console is disconnected or still reconnecting.");
    EXPECT_EQ(error.wizard, hio::WizardId::ConnectDevice);
}

// Review R52 (2026-10-11): a J-Link that does not answer (JLINKARM_GetHWStatus fails) has its own
// Probe code instead of probe.not_found.
TEST(MakeError, ProbeIoFailedCode) {
    EXPECT_EQ(hio::idOf(ErrorCode::ProbeIoFailed), "probe.io_failed");
    EXPECT_EQ(hio::categoryOf(ErrorCode::ProbeIoFailed), ErrorCategory::Probe);
    EXPECT_EQ(hio::errorCodeFromId("probe.io_failed"), ErrorCode::ProbeIoFailed);
    const hio::Error error = hio::makeError(ErrorCode::ProbeIoFailed);
    EXPECT_EQ(error.message, "Communication with the J-Link failed.");
    EXPECT_EQ(error.cause,
              "The J-Link did not answer: a USB glitch, or the USB cable was disconnected.");
    EXPECT_EQ(error.suggestion, "Check the USB cable between the J-Link and the computer; the "
                                "console reconnects automatically if enabled.");
    EXPECT_EQ(error.wizard, hio::WizardId::Troubleshooting);
}

// Review R44: a closed stdout ends with `cancelled` and its own message; the cause, suggestion
// and the detail of the call site stay.
TEST(MakeError, OutputClosedIsCancelledWithItsOwnMessage) {
    const hio::Error error = hio::outputClosedError("the standard output was closed");
    const hio::Error cancelled = hio::makeError(ErrorCode::Cancelled);
    EXPECT_EQ(error.code, ErrorCode::Cancelled);
    EXPECT_EQ(error.message, "The output was closed.");
    EXPECT_EQ(error.cause, cancelled.cause);
    EXPECT_EQ(error.suggestion, cancelled.suggestion);
    EXPECT_EQ(error.detail, "the standard output was closed");
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
