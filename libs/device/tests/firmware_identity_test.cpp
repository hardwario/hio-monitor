// Plan 0028 (ADR-0200, D1, D10): the firmware identity, its slugs and the console file name base.

#include "hwio/device/device_info.h"
#include "hwio/device/firmware_identity.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace {

using hwio::device::consoleFileBase;
using hwio::device::fileSlug;
using hwio::device::FirmwareIdentity;
using hwio::device::identityFromBanner;
using hwio::device::identityFromInfo;
using hwio::device::parseInfoShow;
using hwio::device::versionSlug;

TEST(FileSlug, Rules) {
    EXPECT_EQ(fileSlug("CHESTER Clime"), "chester-clime");
    EXPECT_EQ(fileSlug("hio-chester-push-fm"), "hio-chester-push-fm");
    EXPECT_EQ(fileSlug("  --Žluťoučký kůň!! "), "lu-ou-k-k"); // non-ASCII bytes are runs
    EXPECT_EQ(fileSlug("app \xF0\x9F\x98\x80 x"), "app-x");   // emoji
    EXPECT_EQ(fileSlug(""), "");
    EXPECT_EQ(fileSlug("...."), "");
    EXPECT_EQ(fileSlug(std::string(100, 'a')).size(), hwio::device::maxFileSlugBytes);
    // A cut never leaves a trailing '-'.
    EXPECT_EQ(fileSlug(std::string(39, 'a') + " bcd"), std::string(39, 'a'));
}

TEST(VersionSlug, Rules) {
    EXPECT_EQ(versionSlug("v3.0.0"), "v3.0.0");
    EXPECT_EQ(versionSlug("v3.0.0-5-gabc1234-dirty"), "v3.0.0-5-gabc1234-dirty");
    EXPECT_EQ(versionSlug("v1.part2"), "v1-part2"); // ".part" can never appear
    EXPECT_EQ(versionSlug("v1.0 (beta)"), "v1.0-beta");
    EXPECT_EQ(versionSlug("1.2.3+build_7"), "1.2.3+build_7");
    EXPECT_EQ(versionSlug("v2."), "v2");
    EXPECT_EQ(versionSlug("(unset)"), "unset");
    EXPECT_EQ(versionSlug("..."), "");
    EXPECT_EQ(versionSlug(std::string(60, '1')).size(), hwio::device::maxVersionSlugBytes);
}

TEST(ConsoleFileBase, D1ProfileAndTargetPlusSlugWithoutTheProfileWord) {
    const FirmwareIdentity clime{.name = "CHESTER Clime", .version = "v3.0.0", .deviceSerial = ""};
    EXPECT_EQ(consoleFileBase("chester", "app", clime), "chester-app-clime-v3.0.0");
    EXPECT_EQ(consoleFileBase("chester", "ble", clime), "chester-ble-clime-v3.0.0");
    EXPECT_EQ(consoleFileBase("chester", "app", std::nullopt), "chester-app");
    const FirmwareIdentity push{
        .name = "hio-chester-push-fm", .version = "v4.0.1", .deviceSerial = ""};
    EXPECT_EQ(consoleFileBase("chester", "app", push), "chester-app-hio-chester-push-fm-v4.0.1");
    const FirmwareIdentity bare{.name = "CHESTER", .version = "v1.0.0", .deviceSerial = ""};
    EXPECT_EQ(consoleFileBase("chester", "app", bare), "chester-app-chester-v1.0.0");
    // An empty slug: no identity part.
    const FirmwareIdentity empty{.name = "ČŘŽ", .version = "v1", .deviceSerial = ""};
    EXPECT_EQ(consoleFileBase("chester", "app", empty), "chester-app");
}

TEST(IdentityFromInfo, NeedsNameAndVersionAndCutsAtTheBounds) {
    EXPECT_FALSE(identityFromInfo(parseInfoShow(std::vector<std::string>{"firmware name: x"})));
    EXPECT_FALSE(identityFromInfo(
        parseInfoShow(std::vector<std::string>{"firmware name: (unset)", "firmware version: v1"})));
    const auto identity = identityFromInfo(parseInfoShow(std::vector<std::string>{
        "firmware name: " + std::string(63, 'n') + "\xc3\xa9", // the é crosses byte 64
        "firmware version: " + std::string(60, 'v'), "serial number: " + std::string(40, '1')}));
    ASSERT_TRUE(identity);
    const FirmwareIdentity cut = identity.value_or(FirmwareIdentity{});
    EXPECT_EQ(cut.name, std::string(63, 'n'));
    EXPECT_EQ(cut.version.size(), FirmwareIdentity::maxVersionBytes);
    EXPECT_EQ(cut.deviceSerial.size(), FirmwareIdentity::maxSerialBytes);
}

TEST(IdentityFromBanner, OnlyApplicationBanners) {
    const auto app = identityFromBanner("*** Booting CHESTER Clime v3.0.0-5-gabc1234 ***");
    ASSERT_TRUE(app);
    const FirmwareIdentity banner = app.value_or(FirmwareIdentity{});
    EXPECT_EQ(banner.name, "CHESTER Clime");
    EXPECT_EQ(banner.version, "v3.0.0-5-gabc1234");
    EXPECT_EQ(banner.deviceSerial, "");
    EXPECT_TRUE(identityFromBanner("\x1b[0m*** Booting My App v1.2 ***\r"));
    EXPECT_FALSE(identityFromBanner("*** Booting nRF Connect SDK v2.8.0-a2386bfc8401 ***"));
    EXPECT_FALSE(identityFromBanner("*** Booting Zephyr OS build v3.7.99-ncs1 ***"));
    EXPECT_FALSE(identityFromBanner("*** Booting zephyr os v3.5.0 ***"));
    EXPECT_FALSE(identityFromBanner("*** Booting MCUboot v2.1.0 ***"));
    EXPECT_FALSE(identityFromBanner("*** Using Zephyr OS v3.7.99-0bc3393fb112 ***"));
    EXPECT_FALSE(identityFromBanner("*** Booting App 3.0.0 ***")); // no "v<digit>"
    EXPECT_FALSE(identityFromBanner("*** Booting v3.0.0 ***"));    // no name
    EXPECT_FALSE(identityFromBanner("Booting App v1.0 ***"));
    EXPECT_FALSE(identityFromBanner("*** Booting " + std::string(65, 'a') + " v1 ***"));
}

} // namespace
