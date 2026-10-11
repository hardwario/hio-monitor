// JLinkProgrammer on a GMock of IJLinkApi and simulated time (plan/features/0003, phase B2).

#include "fake_clock.h"
#include "hio/core/error.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/program_progress.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/jlink_probe.h"
#include "hio/hw/jlink/programmer.h"
#include "mock_jlink_api.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using hio::ErrorCode;
using hio::firmware::FirmwareImage;
using hio::firmware::ProgramPhase;
using hio::firmware::ProgramProgress;
using hio::firmware::Segment;
using hio::jlink::ChipFamily;
using hio::jlink::FlashProgressFn;
using hio::jlink::JLinkProbe;
using hio::jlink::JLinkProgrammer;
using hio::jlink::ProgrammerConfig;
using hio::jlink::ResetMode;
using hio::jlink::test::FakeClock;
using hio::jlink::test::MockJLinkApi;
using ::testing::_;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;

constexpr ChipFamily nrf52{
    .name = "nRF52", .ficrPart = 0x10000100, .ficrVariant = 0x10000104, .ctrlApIndex = 1};
constexpr ChipFamily nrf91{
    .name = "nRF91", .ficrPart = 0x00FF0140, .ficrVariant = 0x00FF0144, .ctrlApIndex = 4};

FirmwareImage image(std::uint32_t address, std::size_t size, std::uint8_t value = 0xA5) {
    std::vector<Segment> segments;
    segments.push_back(
        Segment{.address = address, .data = std::vector<std::byte>(size, std::byte{value})});
    return *FirmwareImage::fromSegments(std::move(segments));
}

class Programmer : public ::testing::Test {
protected:
    Programmer() {
        ON_CALL(m_api, readU32(_)).WillByDefault(Return(std::unexpected(-1)));
        ON_CALL(m_api, writeMemory(_, _))
            .WillByDefault([](std::uint32_t, std::span<const std::byte> data) {
                return static_cast<int>(data.size());
            });
        ON_CALL(m_api, endDownload(_)).WillByDefault(Return(0));
        ON_CALL(m_api, coresightConfigure()).WillByDefault(Return(0));
        ON_CALL(m_api, coresightWrite(_, _, _)).WillByDefault(Return(0));
    }

    NiceMock<MockJLinkApi>& api() { return m_api; }
    FakeClock& clock() { return m_clock; }
    JLinkProgrammer& programmer() { return m_programmer; }

    [[nodiscard]] const std::vector<ProgramProgress>& progress() const { return m_progress; }
    [[nodiscard]] const hio::firmware::ProgressFn& recorder() const { return m_recorder; }

private:
    std::vector<ProgramProgress> m_progress;
    hio::firmware::ProgressFn m_recorder = [this](const ProgramProgress& p) {
        m_progress.push_back(p);
    };
    NiceMock<MockJLinkApi> m_api;
    FakeClock m_clock;
    JLinkProbe m_probe{m_api, m_clock};
    JLinkProgrammer m_programmer{m_api, m_probe, m_clock,
                                 ProgrammerConfig{.families = {nrf52, nrf91}, .timing = {}}};
};

TEST_F(Programmer, IdentifiesTheExpectedFamily) {
    EXPECT_CALL(api(), readU32(0x10000100U)).WillOnce(Return(0x52840U));
    EXPECT_CALL(api(), readU32(0x10000104U)).WillOnce(Return(0x41414430U));
    const auto chip = programmer().identify();
    ASSERT_TRUE(chip) << chip.error().detail;
    EXPECT_EQ(chip->part, 0x52840U);
    EXPECT_EQ(chip->variant, 0x41414430U);
    EXPECT_FALSE(chip->protectedChip);
}

TEST_F(Programmer, FallsBackToTheOtherFamily) {
    EXPECT_CALL(api(), readU32(0x10000100U)).WillOnce(Return(std::unexpected(-1)));
    EXPECT_CALL(api(), readU32(0x00FF0140U)).WillOnce(Return(0x9160U));
    EXPECT_CALL(api(), readU32(0x00FF0144U)).WillOnce(Return(0x41414141U));
    const auto chip = programmer().identify();
    ASSERT_TRUE(chip);
    EXPECT_EQ(chip->part, 0x9160U);
}

TEST_F(Programmer, ProtectedChipIsReportedFromTheCtrlAp) {
    // FICR unreadable; DP SELECT = CTRL-AP 1, APPROTECTSTATUS bit 0 = 0 (protected).
    EXPECT_CALL(api(), coresightWrite(JLinkProgrammer::dpSelect, false, 1U << 24U))
        .WillOnce(Return(0));
    EXPECT_CALL(api(), coresightRead(JLinkProgrammer::ctrlApApProtectStatus, true))
        .WillOnce(Return(0U));
    const auto chip = programmer().identify();
    ASSERT_TRUE(chip);
    EXPECT_TRUE(chip->protectedChip);
}

TEST_F(Programmer, UnreadableUnprotectedChipFails) {
    ON_CALL(api(), coresightRead(_, _)).WillByDefault(Return(1U));
    const auto chip = programmer().identify();
    ASSERT_FALSE(chip);
    EXPECT_EQ(chip.error().code, ErrorCode::TargetConnectFailed);
}

TEST_F(Programmer, ProgramDownloadsInChunks) {
    const FirmwareImage firmware = image(0x1000, 10000);
    {
        InSequence order;
        EXPECT_CALL(api(), beginDownload());
        EXPECT_CALL(api(), writeMemory(0x1000U, _))
            .WillOnce([](std::uint32_t, std::span<const std::byte> d) {
                EXPECT_EQ(d.size(), 4096U);
                return static_cast<int>(d.size());
            });
        EXPECT_CALL(api(), writeMemory(0x2000U, _)).WillOnce(Return(4096));
        EXPECT_CALL(api(), writeMemory(0x3000U, _))
            .WillOnce([](std::uint32_t, std::span<const std::byte> d) {
                EXPECT_EQ(d.size(), 10000U - 8192U);
                return static_cast<int>(d.size());
            });
        EXPECT_CALL(api(), endDownload(_)).WillOnce([](const FlashProgressFn& loader) {
            loader("Erase", 100);
            loader("Program", 50);
            loader("Erase", 100); // never back to Erasing
            loader("Program", 100);
            return 0;
        });
    }
    ASSERT_TRUE(programmer().program(firmware, recorder(), {}));
    ASSERT_FALSE(progress().empty());
    EXPECT_EQ(progress().front().phase, ProgramPhase::Erasing);
    EXPECT_EQ(progress().back().phase, ProgramPhase::Programming);
    EXPECT_EQ(progress().back().done, 10000U);
    bool programming = false;
    for (const ProgramProgress& p : progress()) {
        programming = programming || p.phase == ProgramPhase::Programming;
        if (programming) {
            EXPECT_EQ(p.phase, ProgramPhase::Programming);
        }
    }
}

TEST_F(Programmer, ProgramFailure) {
    EXPECT_CALL(api(), endDownload(_)).WillOnce(Return(-5));
    EXPECT_CALL(api(), takeErrorText()).WillRepeatedly(Return(std::string("Flash timeout")));
    const auto status = programmer().program(image(0, 16), recorder(), {});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::TargetProgramFailed);
    EXPECT_NE(status.error().detail.find("returned -5"), std::string::npos);
    EXPECT_NE(status.error().detail.find("Flash timeout"), std::string::npos);
}

TEST_F(Programmer, CancelBeforeTheDownload) {
    std::stop_source stop;
    stop.request_stop();
    EXPECT_CALL(api(), beginDownload()).Times(0);
    const auto status = programmer().program(image(0, 16), recorder(), stop.get_token());
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::Cancelled);
}

TEST_F(Programmer, VerifyReadsInChunksAndFindsTheFirstMismatch) {
    const FirmwareImage firmware = image(0x0, 40000, 0x11);
    int reads = 0;
    ON_CALL(api(), readMemory(_, _))
        .WillByDefault([&reads](std::uint32_t address, std::span<std::byte> buffer) {
            ++reads;
            EXPECT_LE(buffer.size(), 16384U);
            for (std::size_t i = 0; i < buffer.size(); ++i) {
                buffer[i] = address + i == 20000 ? std::byte{0x22} : std::byte{0x11};
            }
            return static_cast<int>(buffer.size());
        });
    const auto status = programmer().verify(firmware, recorder(), {});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::FirmwareVerifyFailed);
    EXPECT_EQ(status.error().message, "Verification failed at address 0x00004e20.");
    EXPECT_NE(status.error().detail.find("expected 0x11, read 0x22"), std::string::npos);
    EXPECT_EQ(reads, 2);
}

TEST_F(Programmer, VerifySucceedsAndCancels) {
    ON_CALL(api(), readMemory(_, _)).WillByDefault([](std::uint32_t, std::span<std::byte> buffer) {
        std::ranges::fill(buffer, std::byte{0xA5});
        return static_cast<int>(buffer.size());
    });
    ASSERT_TRUE(programmer().verify(image(0, 40000), recorder(), {}));
    EXPECT_EQ(progress().back().phase, ProgramPhase::Verifying);
    EXPECT_EQ(progress().back().done, 40000U);

    std::stop_source stop;
    int reads = 0;
    ON_CALL(api(), readMemory(_, _)).WillByDefault([&](std::uint32_t, std::span<std::byte> buffer) {
        if (++reads == 1) {
            stop.request_stop(); // cancel between chunks
        }
        std::ranges::fill(buffer, std::byte{0xA5});
        return static_cast<int>(buffer.size());
    });
    const auto cancelled = programmer().verify(image(0, 40000), recorder(), stop.get_token());
    ASSERT_FALSE(cancelled);
    EXPECT_EQ(cancelled.error().code, ErrorCode::Cancelled);
    EXPECT_EQ(reads, 1);
}

TEST_F(Programmer, VerifyReadFailure) {
    ON_CALL(api(), readMemory(_, _)).WillByDefault(Return(-1));
    const auto status = programmer().verify(image(0, 16), recorder(), {});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::TargetConnectFailed);
}

TEST_F(Programmer, EraseCodeWritesErasedBytesOverTheRange) {
    std::uint64_t written = 0;
    ON_CALL(api(), writeMemory(_, _))
        .WillByDefault([&written](std::uint32_t address, std::span<const std::byte> data) {
            EXPECT_LT(address, 0x100000U);
            for (const std::byte b : data) {
                EXPECT_EQ(b, std::byte{0xFF});
            }
            written += data.size();
            return static_cast<int>(data.size());
        });
    EXPECT_CALL(api(), endDownload(_)).WillOnce([](const FlashProgressFn& loader) {
        loader("Program", 100); // reported as Erasing for an erase
        return 0;
    });
    ASSERT_TRUE(programmer().eraseCode({.start = 0, .end = 0x100000}, recorder(), {}));
    EXPECT_EQ(written, 0x100000U);
    for (const ProgramProgress& p : progress()) {
        EXPECT_EQ(p.phase, ProgramPhase::Erasing);
    }
}

TEST_F(Programmer, EraseAll) {
    EXPECT_CALL(api(), halt()).Times(2).WillRepeatedly(Return(0));
    EXPECT_CALL(api(), eraseChip()).WillOnce(Return(0)).WillOnce(Return(-1));
    ASSERT_TRUE(programmer().eraseAll());
    const auto failed = programmer().eraseAll();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, ErrorCode::TargetEraseFailed);
}

TEST_F(Programmer, RecoverRunsEraseAllOnTheCtrlAp) {
    {
        InSequence order;
        EXPECT_CALL(api(), coresightConfigure()).WillOnce(Return(0));
        EXPECT_CALL(api(), coresightWrite(JLinkProgrammer::dpSelect, false, 1U << 24U))
            .WillOnce(Return(0));
        EXPECT_CALL(api(), coresightWrite(JLinkProgrammer::ctrlApEraseAll, true, 1U))
            .WillOnce(Return(0));
        EXPECT_CALL(api(), coresightRead(JLinkProgrammer::ctrlApEraseAllStatus, true))
            .WillOnce(Return(1U))
            .WillOnce(Return(1U))
            .WillOnce(Return(0U));
        EXPECT_CALL(api(), coresightWrite(JLinkProgrammer::ctrlApReset, true, 1U))
            .WillOnce(Return(0));
        EXPECT_CALL(api(), coresightWrite(JLinkProgrammer::ctrlApReset, true, 0U))
            .WillOnce(Return(0));
        EXPECT_CALL(api(), coresightWrite(JLinkProgrammer::ctrlApEraseAll, true, 0U))
            .WillOnce(Return(0));
    }
    ASSERT_TRUE(programmer().recover({}));
    EXPECT_EQ(clock().slept(), std::chrono::milliseconds(200));
}

TEST_F(Programmer, RecoverTimesOut) {
    ON_CALL(api(), coresightRead(JLinkProgrammer::ctrlApEraseAllStatus, true))
        .WillByDefault(Return(1U));
    const auto status = programmer().recover({});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::TargetRecoverFailed);
    EXPECT_GE(clock().slept(), std::chrono::seconds(60));
}

TEST_F(Programmer, RecoverFailsWithoutCtrlAp) {
    EXPECT_CALL(api(), coresightConfigure()).WillOnce(Return(-1));
    const auto status = programmer().recover({});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::TargetRecoverFailed);
}

TEST_F(Programmer, ReadMemoryBounds) {
    EXPECT_FALSE(programmer().readMemory(0, 0));
    EXPECT_FALSE(programmer().readMemory(0, (64 * 1024) + 1));
    EXPECT_CALL(api(), readMemory(0x10001080U, _))
        .WillOnce([](std::uint32_t, std::span<std::byte> buffer) {
            std::ranges::fill(buffer, std::byte{0xBA});
            return static_cast<int>(buffer.size());
        });
    const auto bytes = programmer().readMemory(0x10001080, 128);
    ASSERT_TRUE(bytes);
    EXPECT_EQ(bytes->size(), 128U);
    EXPECT_CALL(api(), readMemory(_, _)).WillOnce(Return(-1));
    const auto failed = programmer().readMemory(0, 4);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, ErrorCode::TargetConnectFailed);
}

TEST_F(Programmer, ResetGoAndHalt) {
    EXPECT_CALL(api(), reset()).Times(2).WillRepeatedly(Return(0));
    EXPECT_CALL(api(), go()).Times(1);
    ASSERT_TRUE(programmer().reset(ResetMode::Go));
    ASSERT_TRUE(programmer().reset(ResetMode::Halt));
}

} // namespace
