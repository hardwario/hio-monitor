#include "hio/device/profile.h"
#include "hio/firmware/designated_firmware.h"

#include <QString>

#include <gtest/gtest.h>

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
