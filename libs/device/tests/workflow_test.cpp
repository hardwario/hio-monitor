// Flash / erase / recover workflows with a GMock IProgrammer (plan/features/0003, C2), the
// embedded nRF91 image (ADR-0039) and the operation progress reporter.

#include "fake_clock.h"
#include "flash_workflows.h"
#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/operation.h"
#include "hio/device/profile.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/program_progress.h"
#include "hio/hw/jlink/programmer.h"
#include "operation_reporter.h"

#include <QCryptographicHash>
#include <QFile>
#include <QIODevice>
#include <QString>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using hio::ErrorCode;
using hio::Status;
using hio::device::EraseScope;
using hio::device::FlashOptions;
using hio::device::OperationKind;
using hio::device::OperationProgress;
using hio::device::ProfileRegistry;
using hio::device::detail::WorkflowContext;
using hio::firmware::FirmwareImage;
using hio::firmware::ProgramPhase;
using hio::firmware::ProgramProgress;
using hio::firmware::Segment;
using hio::jlink::ChipInfo;
using hio::jlink::ResetMode;
using ::testing::_;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;

class MockProgrammer : public hio::jlink::IProgrammer {
public:
    MOCK_METHOD(hio::Result<ChipInfo>, identify, (), (override));
    MOCK_METHOD(Status, program,
                (const FirmwareImage&, const hio::firmware::ProgressFn&, std::stop_token),
                (override));
    MOCK_METHOD(Status, verify,
                (const FirmwareImage&, const hio::firmware::ProgressFn&, std::stop_token),
                (override));
    MOCK_METHOD(Status, eraseCode,
                (hio::firmware::AddressRange, const hio::firmware::ProgressFn&, std::stop_token),
                (override));
    MOCK_METHOD(Status, eraseAll, (), (override));
    MOCK_METHOD(Status, recover, (std::stop_token), (override));
    MOCK_METHOD((hio::Result<std::vector<std::byte>>), readMemory, (std::uint32_t, std::size_t),
                (override));
    MOCK_METHOD(Status, reset, (ResetMode), (override));
};

ChipInfo chip(std::uint32_t part) {
    return ChipInfo{.part = part, .variant = 0, .protectedChip = false, .name = {}};
}

FirmwareImage image(const std::vector<std::pair<std::uint32_t, std::size_t>>& blocks,
                    std::uint8_t value = 0x5A) {
    std::vector<Segment> segments;
    segments.reserve(blocks.size());
    for (const auto& [address, size] : blocks) {
        segments.push_back(
            Segment{.address = address, .data = std::vector<std::byte>(size, std::byte{value})});
    }
    return *FirmwareImage::fromSegments(std::move(segments));
}

class Workflow : public ::testing::Test {
protected:
    Workflow() {
        ON_CALL(m_programmer, identify()).WillByDefault(Return(chip(0x52840)));
        ON_CALL(m_programmer, program(_, _, _)).WillByDefault(Return(Status{}));
        ON_CALL(m_programmer, verify(_, _, _)).WillByDefault(Return(Status{}));
        ON_CALL(m_programmer, reset(_)).WillByDefault(Return(Status{}));
        ON_CALL(m_programmer, eraseCode(_, _, _)).WillByDefault(Return(Status{}));
        ON_CALL(m_programmer, eraseAll()).WillByDefault(Return(Status{}));
        ON_CALL(m_programmer, recover(_)).WillByDefault(Return(Status{}));
    }

    WorkflowContext context(std::string_view targetId = "app") {
        const auto* profile = ProfileRegistry::find("chester");
        const auto* target = hio::device::findTarget(*profile, targetId);
        return WorkflowContext{
            .profile = profile,
            .target = target,
            .programmer = &m_programmer,
            .progress = [this](const ProgramProgress& p) { m_phases.push_back(p.phase); },
            .stop = {},
            .log = [this](const std::string& text) { m_log.push_back(text); },
            .connectAs =
                [this](std::string_view device) {
                    m_connectedAs.emplace_back(device);
                    return Status{};
                },
            .connectedDevice = std::string(target->jlinkDevice),
            .cpuHalted = false};
    }

    NiceMock<MockProgrammer>& programmer() { return m_programmer; }

    [[nodiscard]] const std::vector<std::string>& log() const { return m_log; }
    [[nodiscard]] const std::vector<std::string>& connectedAs() const { return m_connectedAs; }

private:
    std::vector<ProgramPhase> m_phases;
    std::vector<std::string> m_log;
    std::vector<std::string> m_connectedAs;
    NiceMock<MockProgrammer> m_programmer;
};

TEST_F(Workflow, FlashSequence) {
    {
        InSequence order;
        EXPECT_CALL(programmer(), identify());
        EXPECT_CALL(programmer(), reset(ResetMode::Halt));
        EXPECT_CALL(programmer(), program(_, _, _));
        EXPECT_CALL(programmer(), verify(_, _, _));
        EXPECT_CALL(programmer(), reset(ResetMode::Go));
    }
    auto ctx = context();
    const auto result = hio::device::detail::runFlash(ctx, image({{0x0, 1024}}), FlashOptions{});
    ASSERT_TRUE(result) << result.error().detail;
    EXPECT_EQ(result->name, "nRF52840");
    EXPECT_FALSE(ctx.cpuHalted);
    EXPECT_FALSE(log().empty());
}

TEST_F(Workflow, FlashHaltLeavesTheCpuHalted) {
    EXPECT_CALL(programmer(), reset(ResetMode::Halt)).Times(2);
    auto ctx = context();
    ASSERT_TRUE(hio::device::detail::runFlash(ctx, image({{0x0, 16}}),
                                              FlashOptions{.halt = true, .allowUicr = false}));
    EXPECT_TRUE(ctx.cpuHalted);
}

TEST_F(Workflow, WrongFamily) {
    EXPECT_CALL(programmer(), identify()).WillOnce(Return(chip(0x9120)));
    EXPECT_CALL(programmer(), program(_, _, _)).Times(0);
    auto ctx = context();
    const auto result = hio::device::detail::runFlash(ctx, image({{0x0, 16}}), {});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::TargetWrongFamily);
    EXPECT_EQ(result.error().message, "Expected the app MCU (nRF52840) but found nRF9151.");
    EXPECT_EQ(result.error().suggestion, "Connect the J-Link to the app connector.");
    EXPECT_EQ(result.error().wizard, hio::WizardId::ConnectDevice);
}

TEST_F(Workflow, UnknownPart) {
    EXPECT_CALL(programmer(), identify()).WillOnce(Return(chip(0x1234)));
    auto ctx = context();
    const auto result = hio::device::detail::runFlash(ctx, image({{0x0, 16}}), {});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().message,
              "Expected the app MCU (nRF52840) but found an unknown MCU (part 0x1234).");
}

TEST_F(Workflow, ProtectedChip) {
    EXPECT_CALL(programmer(), identify())
        .WillOnce(Return(ChipInfo{.part = 0, .variant = 0, .protectedChip = true, .name = {}}));
    EXPECT_CALL(programmer(), program(_, _, _)).Times(0);
    auto ctx = context("lte");
    const auto result = hio::device::detail::runFlash(ctx, image({{0x0, 16}}), {});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::TargetProtected);
    EXPECT_EQ(result.error().message, "The lte MCU is read-back protected.");
    EXPECT_EQ(result.error().suggestion, "Run `hio chester lte recover` (erases the MCU).");
}

TEST_F(Workflow, OutOfRangeImageIsRejectedBeforeTheProbeIsUsed) {
    EXPECT_CALL(programmer(), identify()).Times(0);
    auto ctx = context();
    const auto ram = hio::device::detail::runFlash(ctx, image({{0x20000000, 16}}), {});
    ASSERT_FALSE(ram);
    EXPECT_EQ(ram.error().code, ErrorCode::FirmwareOutOfRange);
    EXPECT_EQ(ram.error().message, "The firmware does not fit the app MCU (address 0x20000000).");
    const auto tooLong = hio::device::detail::runFlash(ctx, image({{0xFFF00, 0x200}}), {});
    ASSERT_FALSE(tooLong);
    EXPECT_EQ(tooLong.error().message,
              "The firmware does not fit the app MCU (address 0x00100000).");
}

TEST_F(Workflow, UicrRecordsNeedAllowUicrAndThePibIsNeverWritten) {
    EXPECT_CALL(programmer(), identify()).Times(0);
    auto ctx = context();
    const auto uicr = hio::device::detail::runFlash(ctx, image({{0x0, 16}, {0x10001200, 4}}), {});
    ASSERT_FALSE(uicr);
    EXPECT_EQ(uicr.error().code, ErrorCode::FirmwareUicrInImage);
    const auto pib = hio::device::detail::runFlash(ctx, image({{0x100010F0, 0x20}}),
                                                   FlashOptions{.halt = false, .allowUicr = true});
    ASSERT_FALSE(pib);
    EXPECT_EQ(pib.error().code, ErrorCode::FirmwareOutOfRange);
    EXPECT_EQ(pib.error().message,
              "The firmware writes into the Product Information Block (address 0x100010f0).");
}

TEST_F(Workflow, AllowUicrMergesAndKeepsThePib) {
    // The device's UICR: PIB bytes 0xAB at +0x80, the rest erased.
    std::vector<std::byte> device(0x1000, std::byte{0xFF});
    for (std::size_t i = 0x80; i < 0x100; ++i) {
        device[i] = std::byte{0xAB};
    }
    EXPECT_CALL(programmer(), readMemory(0x10001000U, 0x1000U)).WillOnce(Return(device));
    std::optional<FirmwareImage> written;
    EXPECT_CALL(programmer(), program(_, _, _))
        .WillOnce([&written](const FirmwareImage& firmware, const auto&, const std::stop_token&) {
            written = firmware;
            return Status{};
        });
    auto ctx = context();
    ASSERT_TRUE(hio::device::detail::runFlash(ctx, image({{0x0, 16}, {0x10001200, 4}}, 0x11),
                                              FlashOptions{.halt = false, .allowUicr = true}));
    ASSERT_TRUE(written.has_value());
    const FirmwareImage writtenImage = written.value_or(FirmwareImage{});
    ASSERT_EQ(writtenImage.segments().size(), 2U);
    const Segment& page = writtenImage.segments()[1];
    EXPECT_EQ(page.address, 0x10001000U);
    ASSERT_EQ(page.data.size(), 0x1000U);
    EXPECT_EQ(page.data[0x80], std::byte{0xAB});  // the PIB from the device
    EXPECT_EQ(page.data[0x200], std::byte{0x11}); // the image's UICR record
    EXPECT_EQ(page.data[0x300], std::byte{0xFF});
}

TEST_F(Workflow, AllowUicrOnTheLteMcuLeavesThePibOut) {
    ON_CALL(programmer(), identify()).WillByDefault(Return(chip(0x9120)));
    EXPECT_CALL(programmer(), readMemory(0x00FF8000U, 0x1000U))
        .WillOnce(Return(std::vector<std::byte>(0x1000, std::byte{0xFF})));
    std::optional<FirmwareImage> written;
    EXPECT_CALL(programmer(), program(_, _, _))
        .WillOnce([&written](const FirmwareImage& firmware, const auto&, const std::stop_token&) {
            written = firmware;
            return Status{};
        });
    auto ctx = context("lte");
    ASSERT_TRUE(hio::device::detail::runFlash(ctx, image({{0x00FF8000, 4}}),
                                              FlashOptions{.halt = false, .allowUicr = true}));
    ASSERT_TRUE(written.has_value());
    EXPECT_FALSE(written.value_or(FirmwareImage{}).intersects(hio::device::pibRange(*ctx.target)));
}

TEST_F(Workflow, LteReconnectsWithTheExactPartName) {
    ON_CALL(programmer(), identify()).WillByDefault(Return(chip(0x9160)));
    auto ctx = context("lte");
    const auto result = hio::device::detail::runFlash(ctx, image({{0x0, 16}}), {});
    ASSERT_TRUE(result);
    EXPECT_EQ(result->name, "nRF9160");
    ASSERT_EQ(connectedAs().size(), 1U);
    EXPECT_EQ(connectedAs()[0], "NRF9160_xxAA");
}

TEST_F(Workflow, ProgramFailureNamesTheTarget) {
    EXPECT_CALL(programmer(), program(_, _, _))
        .WillOnce(Return(Status(std::unexpected(hio::makeError(ErrorCode::TargetProgramFailed)))));
    EXPECT_CALL(programmer(), verify(_, _, _)).Times(0);
    auto ctx = context();
    const auto result = hio::device::detail::runFlash(ctx, image({{0x0, 16}}), {});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().message, "Programming the app MCU failed.");
    EXPECT_TRUE(ctx.cpuHalted); // halted before programming
}

TEST_F(Workflow, CancelIsPassedThrough) {
    EXPECT_CALL(programmer(), verify(_, _, _))
        .WillOnce(Return(Status(std::unexpected(hio::makeError(ErrorCode::Cancelled)))));
    auto ctx = context();
    const auto result = hio::device::detail::runFlash(ctx, image({{0x0, 16}}), {});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::Cancelled);
}

TEST_F(Workflow, EraseScopes) {
    EXPECT_CALL(programmer(),
                eraseCode(hio::firmware::AddressRange{.start = 0, .end = 0x100000}, _, _));
    auto ctx = context();
    ASSERT_TRUE(hio::device::detail::runErase(ctx, EraseScope::Code));
    EXPECT_TRUE(ctx.cpuHalted);
    EXPECT_CALL(programmer(), eraseAll());
    ASSERT_TRUE(hio::device::detail::runErase(ctx, EraseScope::All));
    EXPECT_CALL(programmer(), eraseAll())
        .WillOnce(Return(Status(std::unexpected(hio::makeError(ErrorCode::TargetEraseFailed)))));
    const auto failed = hio::device::detail::runErase(ctx, EraseScope::All);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().message, "Erasing the app MCU failed.");
}

TEST_F(Workflow, RecoverAppWithoutImage) {
    EXPECT_CALL(programmer(), identify())
        .WillOnce(Return(ChipInfo{.part = 0, .variant = 0, .protectedChip = true, .name = {}}))
        .WillOnce(Return(chip(0x52840)));
    int reconnects = 0;
    {
        InSequence order;
        EXPECT_CALL(programmer(), recover(_));
        EXPECT_CALL(programmer(), reset(ResetMode::Go));
    }
    EXPECT_CALL(programmer(), program(_, _, _)).Times(0);
    auto ctx = context();
    const auto result = hio::device::detail::runRecover(ctx, [&reconnects] {
        ++reconnects;
        return Status{};
    });
    ASSERT_TRUE(result) << result.error().detail;
    EXPECT_EQ(reconnects, 1);
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ(result->value_or(ChipInfo{}).name, "nRF52840");
}

TEST_F(Workflow, RecoverLteProgramsTheDisableApProtectImage) {
    ON_CALL(programmer(), identify()).WillByDefault(Return(chip(0x9120)));
    std::optional<FirmwareImage> written;
    {
        InSequence order;
        EXPECT_CALL(programmer(), recover(_));
        EXPECT_CALL(programmer(), program(_, _, _))
            .WillOnce(
                [&written](const FirmwareImage& firmware, const auto&, const std::stop_token&) {
                    written = firmware;
                    return Status{};
                });
        EXPECT_CALL(programmer(), verify(_, _, _));
        EXPECT_CALL(programmer(), reset(ResetMode::Go));
    }
    auto ctx = context("lte");
    ASSERT_TRUE(hio::device::detail::runRecover(ctx, [] { return Status{}; }));
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written.value_or(FirmwareImage{}).segments().front().address, 0U);
}

TEST_F(Workflow, RecoverFailure) {
    ON_CALL(programmer(), identify()).WillByDefault(Return(chip(0x9120)));
    EXPECT_CALL(programmer(), recover(_))
        .WillOnce(Return(Status(std::unexpected(hio::makeError(ErrorCode::TargetRecoverFailed)))));
    auto ctx = context("lte");
    const auto result = hio::device::detail::runRecover(ctx, [] { return Status{}; });
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().message, "Recovering the lte MCU failed.");
}

// ADR-0039: the embedded image is py-hardwario's file, byte for byte, and covers 0x0-0xBA4.
TEST(DisableApProtectImage, HashAndLayout) {
    QFile file(QStringLiteral(":/hio/device/nrf91_disable_ap_protect.hex"));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray text = file.readAll();
    EXPECT_EQ(text.size(), 8231);
    EXPECT_EQ(QCryptographicHash::hash(text, QCryptographicHash::Sha256).toHex(),
              QByteArray("51a5af5228427a81d49e3233d5f33271f5f39425f7073a384652075ab4b8e8e8"));
    const auto image =
        hio::device::detail::recoverImage(hio::device::RecoverImage::Nrf91DisableApProtect);
    ASSERT_TRUE(image) << image.error().detail;
    ASSERT_EQ(image->segments().size(), 1U);
    EXPECT_EQ(image->segments()[0].address, 0U);
    EXPECT_EQ(image->segments()[0].range().end, 0xBA4U);
}

// ---- OperationReporter -----------------------------------------------------------------------

TEST(OperationReporterTest, PhasesMoveForwardAndUpdatesAreCoalesced) {
    hio::jlink::test::FakeClock clock;
    std::vector<OperationProgress> emitted;
    const auto planned = hio::device::plannedPhases(OperationKind::Flash, false);
    hio::device::detail::OperationReporter reporter(
        OperationKind::Flash, planned, clock, std::chrono::milliseconds(100),
        [&emitted](const OperationProgress& p) { emitted.push_back(p); });
    reporter.phase(ProgramPhase::Connecting);
    reporter.update({.phase = ProgramPhase::Erasing, .done = 0, .total = 0});
    reporter.update({.phase = ProgramPhase::Programming, .done = 10, .total = 100});
    reporter.update({.phase = ProgramPhase::Programming, .done = 20, .total = 100}); // coalesced
    reporter.update({.phase = ProgramPhase::Erasing, .done = 5, .total = 9});        // not back
    ASSERT_EQ(emitted.size(), 3U);
    clock.advance(std::chrono::milliseconds(150));
    reporter.update({.phase = ProgramPhase::Programming, .done = 30, .total = 100});
    ASSERT_EQ(emitted.size(), 4U);
    EXPECT_EQ(emitted.back().phase, ProgramPhase::Programming);
    EXPECT_EQ(emitted.back().done, 30U);
    EXPECT_EQ(emitted.back().elapsed, std::chrono::milliseconds(150));
    reporter.update({.phase = ProgramPhase::Programming, .done = 40, .total = 100});
    reporter.flush(); // the final state
    ASSERT_EQ(emitted.size(), 5U);
    EXPECT_EQ(emitted.back().done, 40U);
    reporter.update({.phase = ProgramPhase::Recovering, .done = 0, .total = 0}); // unplanned
    EXPECT_EQ(emitted.back().phase, ProgramPhase::Programming);
    // A phase change first emits the coalesced last state of the previous phase.
    reporter.update({.phase = ProgramPhase::Programming, .done = 100, .total = 100});
    const std::size_t before = emitted.size();
    reporter.update({.phase = ProgramPhase::Verifying, .done = 0, .total = 100});
    ASSERT_EQ(emitted.size(), before + 2);
    EXPECT_EQ(emitted[before].phase, ProgramPhase::Programming);
    EXPECT_EQ(emitted[before].done, 100U);
    EXPECT_EQ(emitted.back().phase, ProgramPhase::Verifying);
    for (const OperationProgress& p : emitted) {
        EXPECT_EQ(p.kind, OperationKind::Flash);
        EXPECT_EQ(p.plannedPhases.size(), planned.size());
    }
}

TEST(OperationReporterTest, PlannedPhasesPerKind) {
    using hio::device::plannedPhases;
    EXPECT_EQ(plannedPhases(OperationKind::Flash, true).front(), ProgramPhase::SwitchingTarget);
    EXPECT_EQ(plannedPhases(OperationKind::Flash, true).back(), ProgramPhase::Restoring);
    EXPECT_EQ(plannedPhases(OperationKind::Reset, false).size(), 2U);
    EXPECT_EQ(plannedPhases(OperationKind::LteBundle, false).front(), ProgramPhase::Modem);
    EXPECT_EQ(plannedPhases(OperationKind::Recover, false, true).size(), 6U);
    EXPECT_EQ(hio::device::operationKindId(OperationKind::LteBundle), "lteBundle");
}

} // namespace
