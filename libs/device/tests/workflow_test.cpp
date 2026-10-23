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
#include "hio/hw/jlink/types.h"
#include "operation_reporter.h"
#include "session_worker_target.h"

#include <QCryptographicHash>
#include <QFile>
#include <QIODevice>
#include <QString>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
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
    MOCK_METHOD((hio::Result<std::vector<std::byte>>), readMemory,
                (std::uint32_t, std::size_t, hio::jlink::AccessWidth), (override));
    MOCK_METHOD(Status, reset, (ResetMode), (override));
    MOCK_METHOD(Status, writeMemory,
                (std::uint32_t, std::span<const std::byte>, hio::jlink::AccessWidth), (override));
    MOCK_METHOD(Status, halt, (), (override));
    MOCK_METHOD(Status, go, (), (override));
    MOCK_METHOD(hio::Result<bool>, isHalted, (), (override));
    MOCK_METHOD(hio::Result<std::uint32_t>, coreId, (), (override));
    MOCK_METHOD((hio::Result<std::vector<hio::jlink::RegisterValue>>), readRegisters, (),
                (override));
    MOCK_METHOD((hio::Result<std::vector<hio::jlink::MemoryZone>>), memoryZones, (), (override));
};

ChipInfo chip(std::uint32_t part) {
    return ChipInfo{.part = part,
                    .variant = 0,
                    .protectedChip = false,
                    .name = {},
                    .family = part == 0x52840 ? "nRF52" : "nRF91"};
}

// FICR unreadable: what the CTRL-AP IDR said (review R2).
ChipInfo unreadable(std::string_view family, bool protectedChip) {
    return ChipInfo{
        .part = 0, .variant = 0, .protectedChip = protectedChip, .name = {}, .family = family};
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
    EXPECT_EQ(result.error().message, "Expected the application MCU (nRF52840) but found nRF9151.");
    EXPECT_EQ(result.error().suggestion, "Connect the J-Link to the application connector.");
    EXPECT_EQ(result.error().wizard, hio::WizardId::ConnectDevice);
}

// HIL H4 (Phase 0 item 0.5b): a session for lte with the probe on the app connector finds the
// nRF52840; the texts name the target by its display name, not its id.
TEST_F(Workflow, WrongFamilyOnTheLteTargetNamesTheLteConnector) {
    EXPECT_CALL(programmer(), identify()).WillOnce(Return(chip(0x52840)));
    EXPECT_CALL(programmer(), program(_, _, _)).Times(0);
    auto ctx = context("lte");
    const auto result = hio::device::detail::runFlash(ctx, image({{0x0, 16}}), {});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::TargetWrongFamily);
    EXPECT_EQ(result.error().message, "Expected the LTE MCU (nRF9151) but found nRF52840.");
    EXPECT_EQ(result.error().suggestion, "Connect the J-Link to the LTE connector.");
}

TEST_F(Workflow, UnknownPart) {
    EXPECT_CALL(programmer(), identify()).WillOnce(Return(chip(0x1234)));
    auto ctx = context();
    const auto result = hio::device::detail::runFlash(ctx, image({{0x0, 16}}), {});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().message,
              "Expected the application MCU (nRF52840) but found an unknown MCU (part 0x1234).");
}

TEST_F(Workflow, ProtectedChip) {
    EXPECT_CALL(programmer(), identify()).WillOnce(Return(unreadable("nRF91", true)));
    EXPECT_CALL(programmer(), program(_, _, _)).Times(0);
    auto ctx = context("lte");
    const auto result = hio::device::detail::runFlash(ctx, image({{0x0, 16}}), {});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::TargetProtected);
    EXPECT_EQ(result.error().message, "The LTE MCU is read-back protected.");
    EXPECT_EQ(result.error().suggestion, "Run `hio chester lte recover` (erases the MCU).");
}

TEST_F(Workflow, OutOfRangeImageIsRejectedBeforeTheProbeIsUsed) {
    EXPECT_CALL(programmer(), identify()).Times(0);
    auto ctx = context();
    const auto ram = hio::device::detail::runFlash(ctx, image({{0x20000000, 16}}), {});
    ASSERT_FALSE(ram);
    EXPECT_EQ(ram.error().code, ErrorCode::FirmwareOutOfRange);
    EXPECT_EQ(ram.error().message,
              "The firmware does not fit the application MCU (address 0x20000000).");
    const auto tooLong = hio::device::detail::runFlash(ctx, image({{0xFFF00, 0x200}}), {});
    ASSERT_FALSE(tooLong);
    EXPECT_EQ(tooLong.error().message,
              "The firmware does not fit the application MCU (address 0x00100000).");
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
    EXPECT_CALL(programmer(), readMemory(0x10001000U, 0x1000U, _)).WillOnce(Return(device));
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
    EXPECT_CALL(programmer(), readMemory(0x00FF8000U, 0x1000U, _))
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
    EXPECT_EQ(result.error().message, "Programming the application MCU failed.");
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
    EXPECT_EQ(failed.error().message, "Erasing the application MCU failed.");
}

TEST_F(Workflow, RecoverAppWithoutImage) {
    EXPECT_CALL(programmer(), identify())
        .WillOnce(Return(unreadable("nRF52", true)))
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
    EXPECT_EQ(result.error().message, "Recovering the LTE MCU failed.");
}

// Review R2: FICR unreadable and APPROTECT not enabled is no "protected"; the other family's
// CTRL-AP is the wrong connector; recover never runs without the expected family.
TEST_F(Workflow, UnreadableChipIsNotReportedAsProtected) {
    EXPECT_CALL(programmer(), identify()).WillOnce(Return(unreadable("nRF52", false)));
    auto ctx = context();
    const auto result = hio::device::detail::runFlash(ctx, image({{0x0, 16}}), {});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::TargetConnectFailed);
    EXPECT_EQ(result.error().message, "The type of the application MCU could not be read.");
}

TEST_F(Workflow, OtherFamilysCtrlApIsTheWrongConnector) {
    ON_CALL(programmer(), identify()).WillByDefault(Return(unreadable("nRF91", false)));
    EXPECT_CALL(programmer(), recover(_)).Times(0);
    auto ctx = context();
    const auto flash = hio::device::detail::runFlash(ctx, image({{0x0, 16}}), {});
    ASSERT_FALSE(flash);
    EXPECT_EQ(flash.error().code, ErrorCode::TargetWrongFamily);
    EXPECT_EQ(flash.error().message,
              "Expected the application MCU (nRF52840) but found an nRF91 MCU.");
    const auto recover = hio::device::detail::runRecover(ctx, [] { return Status{}; });
    ASSERT_FALSE(recover);
    EXPECT_EQ(recover.error().code, ErrorCode::TargetWrongFamily);
}

TEST_F(Workflow, RecoverNeedsTheExpectedFamily) {
    EXPECT_CALL(programmer(), identify())
        .WillOnce(Return(hio::Result<ChipInfo>(
            std::unexpected(hio::makeError(ErrorCode::TargetConnectFailed)))));
    EXPECT_CALL(programmer(), recover(_)).Times(0);
    auto ctx = context();
    const auto result = hio::device::detail::runRecover(ctx, [] { return Status{}; });
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::TargetConnectFailed);
    // An unprotected but unreadable chip of the right family can be recovered.
    EXPECT_CALL(programmer(), identify())
        .WillOnce(Return(unreadable("nRF52", false)))
        .WillOnce(Return(chip(0x52840)));
    EXPECT_CALL(programmer(), recover(_));
    EXPECT_TRUE(hio::device::detail::runRecover(ctx, [] { return Status{}; }));
}

// Review R5: a code-only erase that changes the PIB fails loudly.
TEST_F(Workflow, CodeEraseThatChangesThePibFails) {
    const std::vector<std::byte> pib(128, std::byte{0xBA});
    EXPECT_CALL(programmer(), readMemory(0x10001080U, 128U, _))
        .WillOnce(Return(pib))
        .WillOnce(Return(std::vector<std::byte>(128, std::byte{0xFF})));
    auto ctx = context();
    const auto result = hio::device::detail::runErase(ctx, EraseScope::Code);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::TargetEraseFailed);
    EXPECT_EQ(result.error().message, "Erasing the code flash of the application MCU also changed "
                                      "its Product Information Block.");
    EXPECT_CALL(programmer(), readMemory(0x10001080U, 128U, _))
        .Times(2)
        .WillRepeatedly(Return(pib));
    EXPECT_TRUE(hio::device::detail::runErase(ctx, EraseScope::Code));
}

// Review R17: a PIB that cannot be read after the erase is an erase failure that says so.
TEST_F(Workflow, PibUnreadableAfterTheCodeEraseFails) {
    EXPECT_CALL(programmer(), readMemory(0x10001080U, 128U, _))
        .WillOnce(Return(std::vector<std::byte>(128, std::byte{0xBA})))
        .WillOnce(Return(hio::Result<std::vector<std::byte>>(
            std::unexpected(hio::makeError(ErrorCode::TargetConnectFailed, "read failed")))));
    auto ctx = context();
    const auto result = hio::device::detail::runErase(ctx, EraseScope::Code);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::TargetEraseFailed);
    EXPECT_EQ(result.error().message, "The Product Information Block of the application MCU could "
                                      "not be checked after the erase.");
    EXPECT_NE(result.error().detail.find("before: baba"), std::string::npos);
    EXPECT_EQ(result.error().suggestion,
              "Do not erase again. Run `hio chester app pib read` and compare it with the bytes in "
              "the log (see Details); if they differ, contact HARDWARIO support.");
}

// Review R7: reset and memory reads check the family first.
TEST_F(Workflow, ResetAndReadMemoryCheckTheFamily) {
    ON_CALL(programmer(), identify()).WillByDefault(Return(chip(0x9120)));
    EXPECT_CALL(programmer(), reset(_)).Times(0);
    EXPECT_CALL(programmer(), readMemory(_, _, _)).Times(0);
    auto ctx = context();
    const auto reset = hio::device::detail::runReset(ctx, ResetMode::Go);
    ASSERT_FALSE(reset);
    EXPECT_EQ(reset.error().code, ErrorCode::TargetWrongFamily);
    const auto read =
        hio::device::detail::runReadMemory(ctx, 0x10001080, 128, hio::jlink::AccessWidth::Bits8);
    ASSERT_FALSE(read);
    EXPECT_EQ(read.error().code, ErrorCode::TargetWrongFamily);
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

// ---- M6 target operations (plan/features/0013, session_worker_target.cpp)
// ------------------------

using hio::jlink::AccessWidth;

TEST_F(Workflow, TargetOperationsCheckTheFamilyFirst) {
    ON_CALL(programmer(), identify()).WillByDefault(Return(chip(0x9120))); // the other connector
    EXPECT_CALL(programmer(), halt()).Times(0);
    EXPECT_CALL(programmer(), writeMemory(_, _, _)).Times(0);
    auto ctx = context();
    EXPECT_EQ(hio::device::detail::runHalt(ctx).error().code, ErrorCode::TargetWrongFamily);
    const std::vector<std::byte> data(4);
    EXPECT_EQ(hio::device::detail::runWriteMemory(ctx, 0x20000000, data, AccessWidth::Bits32)
                  .error()
                  .code,
              ErrorCode::TargetWrongFamily);
}

TEST_F(Workflow, HaltGoAndStatus) {
    {
        InSequence order;
        EXPECT_CALL(programmer(), identify()).WillOnce(Return(chip(0x52840)));
        EXPECT_CALL(programmer(), halt()).WillOnce(Return(Status{}));
        EXPECT_CALL(programmer(), identify()).WillOnce(Return(chip(0x52840)));
        EXPECT_CALL(programmer(), go()).WillOnce(Return(Status{}));
        EXPECT_CALL(programmer(), identify()).WillOnce(Return(chip(0x52840)));
        EXPECT_CALL(programmer(), isHalted()).WillOnce(Return(true));
        EXPECT_CALL(programmer(), coreId()).WillOnce(Return(0x2BA01477U));
    }
    auto ctx = context();
    const auto halted = hio::device::detail::runHalt(ctx);
    ASSERT_TRUE(halted);
    EXPECT_TRUE(halted->halted);
    EXPECT_EQ(halted->deviceName, "NRF52840_xxAA");
    const auto running = hio::device::detail::runGo(ctx);
    ASSERT_TRUE(running);
    EXPECT_FALSE(running->halted);
    const auto status = hio::device::detail::runTargetStatus(ctx);
    ASSERT_TRUE(status);
    EXPECT_TRUE(status->halted);
    EXPECT_EQ(status->coreId, 0x2BA01477U);
    EXPECT_EQ(status->deviceName, "NRF52840_xxAA");
    EXPECT_FALSE(ctx.cpuHalted); // the worker decides about RTT (ADR-0157)
}

TEST_F(Workflow, WriteMemoryPassesWidthAndUnits) {
    const std::vector<std::byte> data{std::byte{1}, std::byte{2}};
    EXPECT_CALL(programmer(), writeMemory(0x40000000U, _, AccessWidth::Bits16))
        .WillOnce([&](std::uint32_t, std::span<const std::byte> bytes, AccessWidth) {
            EXPECT_TRUE(std::ranges::equal(bytes, data));
            return Status{};
        });
    auto ctx = context();
    EXPECT_TRUE(hio::device::detail::runWriteMemory(ctx, 0x40000000, data, AccessWidth::Bits16));
}

// ADR-0160: the worker checks the flash controller again (DeviceSession checks at request time),
// before touching the target; both nRF91 aliases.
TEST_F(Workflow, WriteMemoryNeverTouchesTheFlashController) {
    EXPECT_CALL(programmer(), identify()).Times(0);
    EXPECT_CALL(programmer(), writeMemory(_, _, _)).Times(0);
    const std::vector<std::byte> word(4);
    auto app = context("app");
    const auto erased = hio::device::detail::runWriteMemory(app, 0x4001E50C, word,
                                                            AccessWidth::Bits32); // ERASEALL
    ASSERT_FALSE(erased);
    EXPECT_EQ(erased.error().code, ErrorCode::TargetAddressOutOfRange);
    EXPECT_NE(erased.error().detail.find("denied 0x4001E000-0x4001F000"), std::string::npos);
    auto lte = context("lte");
    for (const std::uint32_t address : {0x5003950CU, 0x4003950CU}) { // ERASEALL, both aliases
        EXPECT_EQ(hio::device::detail::runWriteMemory(lte, address, word, AccessWidth::Bits32)
                      .error()
                      .code,
                  ErrorCode::TargetAddressOutOfRange)
            << address;
    }
}

// ADR-0160: reads are unrestricted except a range that wraps past 0xFFFFFFFF.
TEST_F(Workflow, ReadMemoryRejectsAWrappingRange) {
    EXPECT_CALL(programmer(), readMemory(_, _, _)).Times(0);
    auto ctx = context();
    const auto wrapped = hio::device::detail::runReadMemory(ctx, 0xFFFFFFFC, 8, AccessWidth::Bits8);
    ASSERT_FALSE(wrapped);
    EXPECT_EQ(wrapped.error().code, ErrorCode::TargetAddressOutOfRange);
}

// Review S6: without JLINKARM_GetId the status is still reported, without a core id; without
// JLINKARM_IsHalted it is not.
TEST_F(Workflow, TargetStatusWithoutCoreId) {
    EXPECT_CALL(programmer(), isHalted())
        .WillOnce(Return(false))
        .WillOnce(Return(std::unexpected(
            hio::makeError(ErrorCode::DependencyJLinkFunctionMissing, "JLINKARM_IsHalted"))));
    EXPECT_CALL(programmer(), coreId())
        .WillOnce(Return(std::unexpected(
            hio::makeError(ErrorCode::DependencyJLinkFunctionMissing, "JLINKARM_GetId"))));
    auto ctx = context();
    const auto status = hio::device::detail::runTargetStatus(ctx);
    ASSERT_TRUE(status) << status.error().detail;
    EXPECT_FALSE(status->halted);
    EXPECT_FALSE(status->coreId.has_value());
    EXPECT_EQ(status->deviceName, "NRF52840_xxAA");
    const auto missing = hio::device::detail::runTargetStatus(ctx);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, ErrorCode::DependencyJLinkFunctionMissing);
    EXPECT_EQ(missing.error().detail, "JLINKARM_IsHalted");
}

TEST_F(Workflow, ReadMemoryPassesTheWidth) {
    EXPECT_CALL(programmer(), readMemory(0x20000000U, 8U, AccessWidth::Bits32))
        .WillOnce(Return(std::vector<std::byte>(8)));
    auto ctx = context();
    EXPECT_TRUE(hio::device::detail::runReadMemory(ctx, 0x20000000, 8, AccessWidth::Bits32));
}

TEST_F(Workflow, RegistersAndZonesComeFromTheProgrammer) {
    EXPECT_CALL(programmer(), readRegisters())
        .WillOnce(Return(std::vector<hio::jlink::RegisterValue>{{.name = "R0", .value = 3}}))
        .WillOnce(Return(std::unexpected(hio::makeError(ErrorCode::TargetNotHalted))));
    EXPECT_CALL(programmer(), memoryZones())
        .WillOnce(Return(std::vector<hio::jlink::MemoryZone>{}));
    auto ctx = context();
    EXPECT_EQ(hio::device::detail::runReadRegisters(ctx)->size(), 1U);
    EXPECT_EQ(hio::device::detail::runReadRegisters(ctx).error().code, ErrorCode::TargetNotHalted);
    EXPECT_TRUE(hio::device::detail::runMemoryZones(ctx)->empty());
}

TEST_F(Workflow, WriteFlashIsAFlashThatRunsAfterwards) {
    {
        InSequence order;
        EXPECT_CALL(programmer(), reset(ResetMode::Halt));
        EXPECT_CALL(programmer(), program(_, _, _));
        EXPECT_CALL(programmer(), verify(_, _, _));
        EXPECT_CALL(programmer(), reset(ResetMode::Go));
    }
    auto ctx = context();
    ASSERT_TRUE(hio::device::detail::runWriteFlash(ctx, image({{0x80000, 16}}), false));
    EXPECT_FALSE(ctx.cpuHalted);
}

TEST_F(Workflow, WriteFlashKeepsTheM2RangeRules) {
    EXPECT_CALL(programmer(), program(_, _, _)).Times(0);
    auto ctx = context();
    // The PIB is never written, also with allowUicr; the UICR needs allowUicr; RAM is no flash.
    EXPECT_EQ(hio::device::detail::runWriteFlash(ctx, image({{0x10001080, 4}}), true).error().code,
              ErrorCode::FirmwareOutOfRange);
    EXPECT_EQ(hio::device::detail::runWriteFlash(ctx, image({{0x10001000, 4}}), false).error().code,
              ErrorCode::FirmwareUicrInImage);
    EXPECT_EQ(hio::device::detail::runWriteFlash(ctx, image({{0x20000000, 4}}), false).error().code,
              ErrorCode::FirmwareOutOfRange);
}

TEST(OperationReporterTest, PlannedPhasesOfTheTargetOperations) {
    using hio::device::plannedPhases;
    for (const OperationKind kind :
         {OperationKind::Halt, OperationKind::Go, OperationKind::TargetStatus,
          OperationKind::WriteMemory, OperationKind::ReadRegisters, OperationKind::MemoryZones}) {
        EXPECT_EQ(plannedPhases(kind, false).size(), 1U);
        EXPECT_EQ(plannedPhases(kind, true).size(), 3U);
        EXPECT_TRUE(hio::device::isLightOperation(kind));
    }
    EXPECT_TRUE(hio::device::isLightOperation(OperationKind::ReadMemory));
    EXPECT_FALSE(hio::device::isLightOperation(OperationKind::WriteFlash));
    EXPECT_FALSE(hio::device::isLightOperation(OperationKind::Reset));
    EXPECT_EQ(plannedPhases(OperationKind::WriteFlash, false).size(),
              plannedPhases(OperationKind::Flash, false).size());
    EXPECT_EQ(hio::device::operationKindId(OperationKind::TargetStatus), "targetStatus");
    EXPECT_EQ(hio::device::operationKindId(OperationKind::WriteFlash), "writeFlash");
    EXPECT_EQ(hio::device::operationKindId(OperationKind::MemoryZones), "memoryZones");
}

} // namespace
