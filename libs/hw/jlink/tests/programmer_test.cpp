// JLinkProgrammer on a GMock of IJLinkApi and simulated time (plan/features/0003, phase B2).

#include "fake_clock.h"
#include "hio/core/error.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/program_progress.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/jlink_probe.h"
#include "hio/hw/jlink/programmer.h"
#include "hio/hw/jlink/types.h"
#include "mock_jlink_api.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
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

constexpr ChipFamily nrf52{.name = "nRF52",
                           .ficrPart = 0x10000100,
                           .ficrVariant = 0x10000104,
                           .ctrlApIndex = 1,
                           .ctrlApIdr = 0x02880000};
constexpr ChipFamily nrf91{.name = "nRF91",
                           .ficrPart = 0x00FF0140,
                           .ficrVariant = 0x00FF0144,
                           .ctrlApIndex = 4,
                           .ctrlApIdr = 0x12880000};

// Key of an AP register in the CoreSight simulator below.
constexpr std::uint32_t apKey(std::uint32_t ap, std::uint32_t bank, std::uint32_t index) {
    return (ap << 16U) | (bank << 4U) | index;
}

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
        // A small CoreSight model: DP SELECT picks AP and bank; an AP register nobody set reads
        // 0 (like an AP that does not exist on the chip).
        ON_CALL(m_api, coresightWrite(_, _, _))
            .WillByDefault([this](std::uint8_t index, bool ap, std::uint32_t value) {
                if (!ap && index == JLinkProgrammer::dpSelect) {
                    m_select = value;
                } else if (ap) {
                    m_writes.push_back(apKey(m_select >> 24U, (m_select >> 4U) & 0xFU, index) |
                                       (value << 28U));
                }
                return 0;
            });
        ON_CALL(m_api, coresightRead(_, _))
            .WillByDefault(
                [this](std::uint8_t index, bool ap) -> std::expected<std::uint32_t, int> {
                    if (!ap) {
                        return 0U;
                    }
                    const auto key = apKey(m_select >> 24U, (m_select >> 4U) & 0xFU, index);
                    const auto it = m_apRegisters.find(key);
                    return it == m_apRegisters.end() ? 0U : it->second;
                });
    }

    // Makes the CTRL-AP of `family` answer (IDR) with APPROTECTSTATUS `status`.
    void ctrlAp(const ChipFamily& family, std::uint32_t status) {
        m_apRegisters[apKey(family.ctrlApIndex, 0xF, JLinkProgrammer::ctrlApIdr)] =
            family.ctrlApIdr | 0x10000000U; // another revision: masked out
        m_apRegisters[apKey(family.ctrlApIndex, 0, JLinkProgrammer::ctrlApApProtectStatus)] =
            status;
    }
    // AP writes as apKey(ap, bank, index) | value << 28 (values 0/1).
    [[nodiscard]] const std::vector<std::uint32_t>& apWrites() const { return m_writes; }
    void openProbe() {
        ON_CALL(m_api, listProbes(_))
            .WillByDefault(Return(std::vector<hio::jlink::ProbeInfo>{
                hio::jlink::ProbeInfo{.serialNumber = 1,
                                      .product = {},
                                      .connection = hio::jlink::Connection::Usb,
                                      .firmware = {}}}));
        ON_CALL(m_api, open()).WillByDefault(Return(std::nullopt));
        ASSERT_TRUE(m_probe.open(1U));
    }

    NiceMock<MockJLinkApi>& api() { return m_api; }
    FakeClock& clock() { return m_clock; }
    JLinkProgrammer& programmer() { return m_programmer; }

    [[nodiscard]] const std::vector<ProgramProgress>& progress() const { return m_progress; }
    [[nodiscard]] const hio::firmware::ProgressFn& recorder() const { return m_recorder; }

private:
    std::uint32_t m_select = 0;
    std::map<std::uint32_t, std::uint32_t> m_apRegisters;
    std::vector<std::uint32_t> m_writes;
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
    // FICR unreadable; CTRL-AP 1 answers with the nRF52 IDR, APPROTECTSTATUS bit 0 = 0.
    ctrlAp(nrf52, 0);
    const auto chip = programmer().identify();
    ASSERT_TRUE(chip) << chip.error().detail;
    EXPECT_TRUE(chip->protectedChip);
    EXPECT_EQ(chip->family, "nRF52");
}

TEST_F(Programmer, UnreadableUnprotectedChipIsNotProtected) {
    ctrlAp(nrf52, 1);
    const auto chip = programmer().identify();
    ASSERT_TRUE(chip);
    EXPECT_FALSE(chip->protectedChip);
    EXPECT_EQ(chip->part, 0U);
}

// Review R2: an AP that does not exist reads 0 - without the IDR check that looked like
// "APPROTECT enabled" on every family.
TEST_F(Programmer, AbsentCtrlApsReadZeroAndAreNotProtected) {
    const auto chip = programmer().identify();
    ASSERT_FALSE(chip);
    EXPECT_EQ(chip.error().code, ErrorCode::TargetConnectFailed);
}

TEST_F(Programmer, CtrlApAtTheOtherFamilysIndexNamesThatFamily) {
    ctrlAp(nrf91, 0); // even "protected" on the other family is not reported as protected
    const auto chip = programmer().identify();
    ASSERT_TRUE(chip);
    EXPECT_EQ(chip->family, "nRF91");
    EXPECT_FALSE(chip->protectedChip);
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

// HIL H5: EraseChip after only a halt erased nothing on a running nRF52840; reset + halt first.
TEST_F(Programmer, EraseAll) {
    EXPECT_CALL(api(), go()).Times(0);
    {
        const InSequence order;
        EXPECT_CALL(api(), reset()).WillOnce(Return(0));
        EXPECT_CALL(api(), eraseChip()).WillOnce(Return(0));
        EXPECT_CALL(api(), reset()).WillOnce(Return(0));
        EXPECT_CALL(api(), eraseChip()).WillOnce(Return(-1));
    }
    ASSERT_TRUE(programmer().eraseAll());
    const auto failed = programmer().eraseAll();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, ErrorCode::TargetEraseFailed);
}

TEST_F(Programmer, EraseAllStopsWhenTheResetFails) {
    EXPECT_CALL(api(), reset()).WillOnce(Return(-1));
    EXPECT_CALL(api(), eraseChip()).Times(0);
    const auto failed = programmer().eraseAll();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, ErrorCode::TargetConnectFailed);
    EXPECT_NE(failed.error().detail.find("JLINKARM_Reset returned -1"), std::string::npos)
        << failed.error().detail;
}

TEST_F(Programmer, RecoverRunsEraseAllOnTheCtrlAp) {
    ctrlAp(nrf52, 0);
    EXPECT_CALL(api(), coresightRead(_, _)).Times(::testing::AnyNumber());
    EXPECT_CALL(api(), coresightRead(JLinkProgrammer::ctrlApEraseAllStatus, true))
        .WillOnce(Return(1U))
        .WillOnce(Return(1U))
        .WillOnce(Return(0U));
    ASSERT_TRUE(programmer().recover({}));
    EXPECT_EQ(clock().slept(), std::chrono::milliseconds(200));
    const std::vector<std::uint32_t> expected{
        apKey(1, 0, JLinkProgrammer::ctrlApEraseAll) | (1U << 28U),
        apKey(1, 0, JLinkProgrammer::ctrlApReset) | (1U << 28U),
        apKey(1, 0, JLinkProgrammer::ctrlApReset), apKey(1, 0, JLinkProgrammer::ctrlApEraseAll)};
    EXPECT_EQ(apWrites(), expected);
}

TEST_F(Programmer, RecoverTimesOut) {
    ctrlAp(nrf52, 0);
    EXPECT_CALL(api(), coresightRead(_, _)).Times(::testing::AnyNumber());
    EXPECT_CALL(api(), coresightRead(JLinkProgrammer::ctrlApEraseAllStatus, true))
        .WillRepeatedly(Return(1U));
    const auto status = programmer().recover({});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::TargetRecoverFailed);
    EXPECT_GE(clock().slept(), std::chrono::seconds(60));
}

// Review R2: ERASEALL is never written to an AP whose IDR does not match.
TEST_F(Programmer, RecoverChecksTheCtrlApIdentification) {
    const auto none = programmer().recover({});
    ASSERT_FALSE(none);
    EXPECT_EQ(none.error().code, ErrorCode::TargetConnectFailed);
    ctrlAp(nrf91, 0);
    const auto other = programmer().recover({});
    ASSERT_FALSE(other);
    EXPECT_EQ(other.error().code, ErrorCode::TargetWrongFamily);
    EXPECT_TRUE(apWrites().empty());
}

TEST_F(Programmer, RecoverFailsWithoutCoreSight) {
    EXPECT_CALL(api(), coresightConfigure()).WillRepeatedly(Return(-1));
    const auto status = programmer().recover({});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::TargetConnectFailed);
}

// Review R6: the loader's "Compare" comes first and stays Erasing.
TEST_F(Programmer, LoaderActionsMapToPhases) {
    EXPECT_CALL(api(), endDownload(_)).WillOnce([](const FlashProgressFn& loader) {
        loader("Compare", 50);
        loader("Erase", 100);
        loader("Program", 40);
        loader("Verify", 100);
        loader("Compare", 100); // never back
        return 0;
    });
    ASSERT_TRUE(programmer().program(image(0, 100), recorder(), {}));
    std::vector<ProgramPhase> phases;
    for (const ProgramProgress& p : progress()) {
        if (phases.empty() || phases.back() != p.phase) {
            phases.push_back(p.phase);
        }
    }
    EXPECT_EQ(phases,
              (std::vector<ProgramPhase>{ProgramPhase::Erasing, ProgramPhase::Programming}));
    EXPECT_EQ(progress().at(1).phase, ProgramPhase::Erasing); // the Compare report
}

// Review R3, ADR-0094: a download that cannot finish is abandoned by closing the probe
// (EndDownload is never called). Phase 0 showed that the close still programs the buffered data, so
// it is reported as an interrupted flash.
void expectInterruptedFlash(const hio::Error& error, std::string_view detail) {
    EXPECT_EQ(error.code, ErrorCode::TargetProgramFailed);
    EXPECT_EQ(error.message, "Programming the MCU failed.");
    EXPECT_EQ(error.cause, "Programming was interrupted; the firmware may be partly written.");
    EXPECT_EQ(error.suggestion, "Check the cables and power, then flash the firmware again.");
    EXPECT_NE(error.detail.find(detail), std::string::npos) << error.detail;
}

TEST_F(Programmer, WriteFailureAbandonsTheDownload) {
    openProbe();
    EXPECT_CALL(api(), writeMemory(_, _)).WillOnce(Return(-1));
    EXPECT_CALL(api(), endDownload(_)).Times(0);
    EXPECT_CALL(api(), close());
    const auto status = programmer().program(image(0, 10000), recorder(), {});
    ASSERT_FALSE(status);
    expectInterruptedFlash(status.error(),
                           "flash download abandoned: JLINKARM_WriteMem(0x00000000, 4096) returned "
                           "-1");
}

TEST_F(Programmer, DeadlineAbandonsTheDownload) {
    openProbe();
    EXPECT_CALL(api(), writeMemory(_, _))
        .WillOnce([this](std::uint32_t, std::span<const std::byte> data) {
            clock().advance(std::chrono::minutes(6));
            return static_cast<int>(data.size());
        });
    EXPECT_CALL(api(), endDownload(_)).Times(0);
    EXPECT_CALL(api(), close());
    const auto status = programmer().program(image(0, 10000), recorder(), {});
    ASSERT_FALSE(status);
    expectInterruptedFlash(status.error(),
                           "flash download abandoned: buffering exceeded the deadline of 300 s");
}

// ADR-0094: the code-only erase is a download of 0xFF; abandoning it is an interrupted flash too.
TEST_F(Programmer, AbandonedEraseCodeIsAnInterruptedFlash) {
    openProbe();
    EXPECT_CALL(api(), writeMemory(_, _)).WillOnce(Return(-1));
    EXPECT_CALL(api(), endDownload(_)).Times(0);
    EXPECT_CALL(api(), close());
    const auto status = programmer().eraseCode({.start = 0, .end = 0x100000}, recorder(), {});
    ASSERT_FALSE(status);
    expectInterruptedFlash(status.error(), "JLINKARM_WriteMem(0x00000000, 4096) returned -1");
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
