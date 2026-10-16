#include "hio/device/profile.h"

#include <gtest/gtest.h>

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
            EXPECT_GT(target.defaultSpeedKhz.value_or(1), 0) << target.id; // nullopt = auto
            EXPECT_GT(target.defaultLatencyMs, 0) << target.id;
            EXPECT_GT(target.minTargetMillivolts, 0) << target.id;
        }
    }
}

} // namespace
