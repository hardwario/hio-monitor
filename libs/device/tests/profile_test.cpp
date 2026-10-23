#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/profile.h"
#include "hio/firmware/designated_firmware.h"
#include "hio/firmware/firmware_image.h"

#include <QString>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace {

using hio::device::ProfileRegistry;

TEST(Profiles, ChesterIsRegistered) {
    const hio::device::DeviceProfile* chester = ProfileRegistry::find("chester");
    ASSERT_NE(chester, nullptr);
    EXPECT_EQ(chester->displayName, "CHESTER");
    EXPECT_EQ(ProfileRegistry::find("nope"), nullptr);
    ASSERT_EQ(ProfileRegistry::all().size(), 1U);
    EXPECT_EQ(ProfileRegistry::all().front(), chester);
}

TEST(Profiles, ChesterAppTarget) {
    const auto* app = hio::device::findTarget(*ProfileRegistry::find("chester"), "app");
    ASSERT_NE(app, nullptr);
    EXPECT_EQ(app->jlinkDevice, "NRF52840_xxAA");
    EXPECT_EQ(hio::device::targetDisplayName(*app), QStringLiteral("application"));
    EXPECT_EQ(app->rttTerminal, "Terminal");
    EXPECT_EQ(app->rttLogger, "Logger");
    EXPECT_TRUE(app->hashPrefixIsLog);
    EXPECT_FALSE(app->defaultSpeedKhz.has_value()); // automatic, as pylink (ADR-0090)
    EXPECT_EQ(app->defaultLatencyMs, 50);
    EXPECT_EQ(app->minTargetMillivolts, 1000);
}

TEST(Profiles, ChesterLteTargetIsDeclaredWithoutConsole) {
    const auto* lte = hio::device::findTarget(*ProfileRegistry::find("chester"), "lte");
    ASSERT_NE(lte, nullptr);
    EXPECT_EQ(lte->jlinkDevice, "NRF9151_XXCA");
    EXPECT_EQ(hio::device::targetDisplayName(*lte), QStringLiteral("LTE"));
    EXPECT_TRUE(lte->rttTerminal.empty());
    EXPECT_FALSE(lte->hashPrefixIsLog);
    EXPECT_EQ(hio::device::findTarget(*ProfileRegistry::find("chester"), "ble"), nullptr);
}

TEST(Profiles, EveryProfileIsConsistent) {
    for (const hio::device::DeviceProfile* profile : ProfileRegistry::all()) {
        EXPECT_FALSE(profile->id.empty());
        EXPECT_FALSE(profile->targets.empty()) << profile->id;
        for (const hio::device::TargetProfile& target : profile->targets) {
            EXPECT_FALSE(target.jlinkDevice.empty()) << target.id;
            EXPECT_FALSE(target.displayName.empty()) << target.id;
            EXPECT_GT(target.defaultSpeedKhz.value_or(1), 0) << target.id; // nullopt = auto
            EXPECT_GT(target.defaultLatencyMs, 0) << target.id;
            EXPECT_GT(target.minTargetMillivolts, 0) << target.id;
        }
    }
}

} // namespace

namespace {

// M2 (plan/features/0003, C1).
TEST(Profiles, ChesterFlashLayout) {
    const auto& chester = *ProfileRegistry::find("chester");
    EXPECT_EQ(chester.firmwareCatalogUrl, "https://firmware.hardwario.com/chester");
    const auto* app = hio::device::findTarget(chester, "app");
    const auto* lte = hio::device::findTarget(chester, "lte");
    ASSERT_NE(app, nullptr);
    ASSERT_NE(lte, nullptr);
    ASSERT_NE(hio::device::findPart(*app, 0x52840), nullptr);
    EXPECT_EQ(hio::device::findPart(*app, 0x52840)->displayName, "nRF52840");
    EXPECT_EQ(hio::device::findPart(*app, 0x9160), nullptr);
    EXPECT_EQ(hio::device::findPart(*lte, 0x9120)->displayName, "nRF9151");
    EXPECT_EQ(hio::device::findPart(*lte, 0x9120)->jlinkDevice, "NRF9151_XXCA");
    EXPECT_EQ(hio::device::findPart(*lte, 0x9160)->jlinkDevice, "NRF9160_xxAA");
    EXPECT_EQ(hio::device::pibRange(*app).start, 0x10001080U);
    EXPECT_EQ(hio::device::pibRange(*lte).start, 0x00FF8108U);
    EXPECT_EQ(hio::device::pibRange(*lte).size(), 128U);
    EXPECT_EQ(app->defaultErase, hio::device::EraseScope::Code);
    EXPECT_EQ(lte->defaultErase, hio::device::EraseScope::All);
    EXPECT_EQ(lte->recoverImage, hio::device::RecoverImage::Nrf91DisableApProtect);
    EXPECT_EQ(app->chipFamily.ctrlApIndex, 1U);
    EXPECT_EQ(lte->chipFamily.ctrlApIndex, 4U);
    EXPECT_TRUE(app->pibErasableWithUicr);
    EXPECT_FALSE(lte->pibErasableWithUicr);
}

TEST(Profiles, FlashLayoutIsConsistent) {
    for (const hio::device::DeviceProfile* profile : ProfileRegistry::all()) {
        for (const hio::device::TargetProfile& target : profile->targets) {
            EXPECT_FALSE(target.parts.empty()) << target.id;
            EXPECT_EQ(target.parts.front().jlinkDevice, target.jlinkDevice) << target.id;
            EXPECT_FALSE(target.flash.empty()) << target.id;
            EXPECT_FALSE(target.uicr.overlaps(target.flash)) << target.id;
            EXPECT_TRUE(target.uicr.contains(hio::device::pibRange(target))) << target.id;
        }
    }
}

} // namespace

namespace {

// M6 (plan/features/0013, ADR-0099): RAM and peripheral ranges (half-open), no RTT prompt yet
// (open question 25).
TEST(Profiles, ChesterMemoryRangesAndPrompt) {
    const auto& chester = *ProfileRegistry::find("chester");
    for (const std::string_view id : {std::string_view("app"), std::string_view("lte")}) {
        const auto* target = hio::device::findTarget(chester, id);
        ASSERT_NE(target, nullptr);
        EXPECT_EQ(target->ram,
                  (hio::firmware::AddressRange{.start = 0x20000000, .end = 0x20040000}))
            << id;
        ASSERT_EQ(target->peripherals.size(), 2U) << id;
        EXPECT_EQ(target->peripherals[0],
                  (hio::firmware::AddressRange{.start = 0x40000000, .end = 0x60000000}));
        EXPECT_EQ(target->peripherals[1],
                  (hio::firmware::AddressRange{.start = 0xE0000000, .end = 0xE0100000}));
        EXPECT_TRUE(target->shellPrompt.empty()) << id;
        EXPECT_FALSE(target->ram.overlaps(target->flash)) << id;
        EXPECT_FALSE(target->ram.overlaps(target->uicr)) << id;
    }
}

// ADR-0160: the flash controller (NVMC) block of each target is denied to memory writes; on the
// nRF91 both the secure and the non-secure alias. ADR-0164: also the FPB and the System Control
// Space on both targets.
TEST(Profiles, ChesterWriteDeniedRanges) {
    constexpr hio::firmware::AddressRange fpb{.start = 0xE0002000, .end = 0xE0003000};
    constexpr hio::firmware::AddressRange scs{.start = 0xE000E000, .end = 0xE000F000};
    const auto& chester = *ProfileRegistry::find("chester");
    const auto* app = hio::device::findTarget(chester, "app");
    ASSERT_NE(app, nullptr);
    ASSERT_EQ(app->writeDenied.size(), 3U);
    EXPECT_EQ(app->writeDenied[0],
              (hio::firmware::AddressRange{.start = 0x4001E000, .end = 0x4001F000}));
    EXPECT_EQ(app->writeDenied[1], fpb);
    EXPECT_EQ(app->writeDenied[2], scs);
    const auto* lte = hio::device::findTarget(chester, "lte");
    ASSERT_NE(lte, nullptr);
    ASSERT_EQ(lte->writeDenied.size(), 4U);
    EXPECT_EQ(lte->writeDenied[0],
              (hio::firmware::AddressRange{.start = 0x40039000, .end = 0x4003A000}));
    EXPECT_EQ(lte->writeDenied[1],
              (hio::firmware::AddressRange{.start = 0x50039000, .end = 0x5003A000}));
    EXPECT_EQ(lte->writeDenied[2], fpb);
    EXPECT_EQ(lte->writeDenied[3], scs);
}

// The error code of checkMemoryWrite(); nullopt = allowed.
std::optional<hio::ErrorCode> writeCode(std::string_view targetId, std::uint32_t address,
                                        std::size_t size) {
    const auto* target = hio::device::findTarget(*ProfileRegistry::find("chester"), targetId);
    const hio::Status status = hio::device::checkMemoryWrite(*target, address, size);
    if (status) {
        return std::nullopt;
    }
    return status.error().code;
}

constexpr std::optional<hio::ErrorCode> denied{hio::ErrorCode::TargetAddressOutOfRange};

// ADR-0160: the NVMC task registers (nRF52840: CONFIG 0x504, ERASEPAGE 0x508, ERASEALL 0x50C,
// ERASEUICR 0x514; nRF9160: CONFIG 0x504, ERASEALL 0x50C, ERASEPAGEPARTIALCFG 0x51C, register
// offsets from the product specifications, to confirm) and every partial overlap are rejected.
TEST(Profiles, NvmcWritesAreDenied) {
    for (const std::uint32_t offset : {0x504U, 0x508U, 0x50CU, 0x514U}) {
        EXPECT_EQ(writeCode("app", 0x4001E000U + offset, 4), denied) << offset;
    }
    for (const std::uint32_t base : {0x40039000U, 0x50039000U}) {
        for (const std::uint32_t offset : {0x504U, 0x50CU, 0x51CU}) {
            EXPECT_EQ(writeCode("lte", base + offset, 4), denied) << base + offset;
        }
        EXPECT_EQ(writeCode("lte", base - 4, 8), denied) << base;            // over the start
        EXPECT_EQ(writeCode("lte", base + 0xFFC, 8), denied) << base;        // over the end
        EXPECT_EQ(writeCode("lte", base - 0x1000, 0x3000), denied) << base;  // around it
        EXPECT_EQ(writeCode("lte", base - 4, 4), std::nullopt) << base;      // adjacent below
        EXPECT_EQ(writeCode("lte", base + 0x1000, 4), std::nullopt) << base; // adjacent above
    }
    EXPECT_EQ(writeCode("app", 0x4001DFFC, 8), denied);
    EXPECT_EQ(writeCode("app", 0x4001EFFC, 8), denied);
    EXPECT_EQ(writeCode("app", 0x4001DFFC, 4), std::nullopt);
    EXPECT_EQ(writeCode("app", 0x4001F000, 4), std::nullopt);
    // The app target denies only its own NVMC (0x40039000 is another peripheral on nRF52840).
    EXPECT_EQ(writeCode("app", 0x4003950C, 4), std::nullopt);

    const auto* app = hio::device::findTarget(*ProfileRegistry::find("chester"), "app");
    const hio::Status status = hio::device::checkMemoryWrite(*app, 0x4001E50C, 4);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().detail,
              "requested 0x4001E50C-0x4001E510; denied 0x4001E000-0x4001F000");
}

// ADR-0164: the registers through which a write could make the CPU run code are denied on both
// targets: SCB->VTOR, NVIC STIR, DCRSR (System Control Space) and the FPB (FP_CTRL, a comparator);
// the neighbouring DWT and the address right after the FPB stay writable.
TEST(Profiles, CodeExecutionRegistersAreDenied) {
    for (const std::string_view target : {"app", "lte"}) {
        EXPECT_EQ(writeCode(target, 0xE000ED08, 4), denied) << target;       // SCB->VTOR
        EXPECT_EQ(writeCode(target, 0xE000EF00, 4), denied) << target;       // NVIC STIR
        EXPECT_EQ(writeCode(target, 0xE000EDF4, 4), denied) << target;       // DCRSR
        EXPECT_EQ(writeCode(target, 0xE000E100, 4), denied) << target;       // NVIC ISER0
        EXPECT_EQ(writeCode(target, 0xE0002000, 4), denied) << target;       // FP_CTRL
        EXPECT_EQ(writeCode(target, 0xE0002008, 4), denied) << target;       // FP_COMP0
        EXPECT_EQ(writeCode(target, 0xE0001FFC, 8), denied) << target;       // over the FPB start
        EXPECT_EQ(writeCode(target, 0xE000EFFC, 8), denied) << target;       // over the SCS end
        EXPECT_EQ(writeCode(target, 0xE0001000, 4), std::nullopt) << target; // DWT_CTRL
        EXPECT_EQ(writeCode(target, 0xE0001FFC, 4), std::nullopt) << target; // below the FPB
        EXPECT_EQ(writeCode(target, 0xE0003000, 4), std::nullopt) << target; // after the FPB
        EXPECT_EQ(writeCode(target, 0xE000DFFC, 4), std::nullopt) << target; // below the SCS
        EXPECT_EQ(writeCode(target, 0xE000F000, 4), std::nullopt) << target; // after the SCS
    }
    const auto* app = hio::device::findTarget(*ProfileRegistry::find("chester"), "app");
    const hio::Status status = hio::device::checkMemoryWrite(*app, 0xE000ED08, 4);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().detail,
              "requested 0xE000ED08-0xE000ED0C; denied 0xE000E000-0xE000F000");
    EXPECT_FALSE(status.error().cause.empty());
}

// The flash rules of planImage (ADR-0035, ADR-0036) as one public helper (review: CLI, MCP).
TEST(Profiles, CheckFlashWrite) {
    const auto& chester = *ProfileRegistry::find("chester");
    const auto code = [&](std::string_view targetId, std::uint32_t address, std::size_t size,
                          bool allowUicr) -> std::optional<hio::ErrorCode> {
        const hio::Status status = hio::device::checkFlashWrite(
            *hio::device::findTarget(chester, targetId), address, size, allowUicr);
        return status ? std::nullopt : std::optional(status.error().code);
    };
    using hio::ErrorCode;
    EXPECT_EQ(code("app", 0x00010000, 8, false), std::nullopt);
    EXPECT_EQ(code("app", 0x000FFFF8, 8, false), std::nullopt); // ends at flash end
    EXPECT_EQ(code("app", 0x000FFFF8, 16, false), ErrorCode::FirmwareOutOfRange); // past it
    EXPECT_EQ(code("app", 0x20000000, 4, true), ErrorCode::FirmwareOutOfRange);   // RAM
    EXPECT_EQ(code("app", 0x10001000, 4, false), ErrorCode::FirmwareUicrInImage);
    EXPECT_EQ(code("app", 0x10001000, 4, true), std::nullopt);
    EXPECT_EQ(code("app", 0x10001080, 1, true), ErrorCode::FirmwareOutOfRange); // the PIB
    EXPECT_EQ(code("app", 0x1000107C, 8, true), ErrorCode::FirmwareOutOfRange); // into the PIB
    EXPECT_EQ(code("app", 0x10001100, 4, true), std::nullopt);                  // after it
    EXPECT_EQ(code("lte", 0x00FF8108, 4, true), ErrorCode::FirmwareOutOfRange); // lte PIB
    EXPECT_EQ(code("lte", 0x00FF8000, 4, false), ErrorCode::FirmwareUicrInImage);
    // Range errors win over the UICR rule.
    EXPECT_EQ(code("app", 0x10001080, 4, false), ErrorCode::FirmwareOutOfRange);
}

// ADR-0160: reads may touch any range that does not wrap past 0xFFFFFFFF.
TEST(Profiles, MemoryReadsAreUnrestrictedButMustNotWrap) {
    EXPECT_TRUE(hio::device::checkMemoryRead(0x10000100, 4));  // FICR
    EXPECT_TRUE(hio::device::checkMemoryRead(0xE00FF000, 64)); // ROM table
    EXPECT_TRUE(hio::device::checkMemoryRead(0x30000000, 4));  // unmapped: the backend fails
    EXPECT_TRUE(hio::device::checkMemoryRead(0xFFFFFFFC, 4));  // ends exactly at 2^32
    const hio::Status wrapped = hio::device::checkMemoryRead(0xFFFFFFFC, 8);
    ASSERT_FALSE(wrapped);
    EXPECT_EQ(wrapped.error().code, hio::ErrorCode::TargetAddressOutOfRange);
}

} // namespace

// ---- plan 0021 (ADR-0165, ADR-0167) ---------------------------------------------------------

namespace {

TEST(Profiles, ChesterLteDesignatedFirmware) {
    const auto& chester = *ProfileRegistry::find("chester");
    const auto* lte = hio::device::findTarget(chester, "lte");
    ASSERT_NE(lte, nullptr);
    ASSERT_TRUE(lte->designatedFirmware.has_value());
    const hio::firmware::DesignatedFirmware designated =
        lte->designatedFirmware.value_or(hio::firmware::DesignatedFirmware{});
    EXPECT_EQ(designated.name, "HARDWARIO LTE firmware");
    EXPECT_EQ(designated.version, "1.7.0");
    EXPECT_EQ(designated.url, "https://docs.hardwario.com/download/hio-chester-lte-v1.7.0.zip");
    EXPECT_EQ(hio::firmware::toHex(designated.sha256),
              "95d301a11738bcd9a9df4726159361d64fd8f681c3546f479d46b49f4bf0fe49");
    EXPECT_EQ(designated.kind, hio::firmware::FirmwareKind::LteBundle);
    // The application MCU has no designated firmware (it is chosen from the catalog).
    EXPECT_FALSE(hio::device::findTarget(chester, "app")->designatedFirmware.has_value());
}

TEST(Profiles, EveryDesignatedFirmwareIsHttps) {
    for (const hio::device::DeviceProfile* profile : ProfileRegistry::all()) {
        for (const hio::device::TargetProfile& target : profile->targets) {
            if (target.designatedFirmware) {
                EXPECT_TRUE(target.designatedFirmware->url.starts_with("https://")) << target.id;
                EXPECT_FALSE(target.designatedFirmware->name.empty()) << target.id;
                EXPECT_FALSE(target.designatedFirmware->version.empty()) << target.id;
            }
        }
    }
}

TEST(Profiles, ChesterFirmwareDocsUrlPerLanguage) {
    const auto& chester = *ProfileRegistry::find("chester");
    ASSERT_EQ(chester.firmwareDocsUrl.size(), 2U);
    EXPECT_EQ(chester.firmwareDocsUrl.front().language, "en"); // the fallback comes first
    const std::string_view english =
        "https://docs.hardwario.com/chester/catalog-applications/catalog-applications/"
        "#application-firmware";
    const std::string_view czech =
        "https://docs.hardwario.com/cs/chester/catalog-applications/catalog-applications/"
        "#application-firmware";
    EXPECT_EQ(hio::device::firmwareDocsUrlFor(chester, "en"), english);
    EXPECT_EQ(hio::device::firmwareDocsUrlFor(chester, "cs"), czech);
    EXPECT_EQ(hio::device::firmwareDocsUrlFor(chester, "cs_CZ"), czech);
    EXPECT_EQ(hio::device::firmwareDocsUrlFor(chester, "cs-CZ"), czech);
    EXPECT_EQ(hio::device::firmwareDocsUrlFor(chester, "de"), english);
    EXPECT_EQ(hio::device::firmwareDocsUrlFor(chester, ""), english);
    // The catalog URL stays the download base (ADR-0037).
    EXPECT_EQ(chester.firmwareCatalogUrl, "https://firmware.hardwario.com/chester");
}

} // namespace
