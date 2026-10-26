// SemVer 2.0.0 parsing and precedence (plan/features/0016 A1).

#include "hio/core/error.h"
#include "hio/update/semver.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

using hio::update::SemVer;

SemVer v(std::string_view text) {
    auto version = SemVer::parse(text);
    EXPECT_TRUE(version.has_value()) << text;
    return version.value_or(SemVer{});
}

TEST(SemVer, ParsesCoreAndLeadingV) {
    const SemVer version = v("v2.10.3");
    EXPECT_EQ(version.major, 2U);
    EXPECT_EQ(version.minor, 10U);
    EXPECT_EQ(version.patch, 3U);
    EXPECT_TRUE(version.prerelease.empty());
    EXPECT_TRUE(version.build.empty());
    EXPECT_EQ(v("2.10.3"), version);
}

TEST(SemVer, ParsesPrereleaseAndBuild) {
    const SemVer version = v("2.0.1-dev.5+abc1234.dirty");
    EXPECT_EQ(version.prerelease, (std::vector<std::string>{"dev", "5"}));
    EXPECT_EQ(version.build, "abc1234.dirty");
    EXPECT_EQ(version.toString(), "2.0.1-dev.5");
    EXPECT_EQ(version.toString(true), "2.0.1-dev.5+abc1234.dirty");
    EXPECT_TRUE(version.isPrerelease());
}

TEST(SemVer, RejectsInvalidText) {
    for (const std::string_view text :
         {"", "v", "2", "2.0", "2.0.0.0", "02.0.0", "2.00.0", "2.0.-1", "2.0.0-", "2.0.0-rc..1",
          "2.0.0-01", "2.0.0+", "2.0.0+a..b", "2.0.0-rc_1", "x2.0.0", "2.0.0 ", "4294967296.0.0",
          "V2.0.0", "2.0.0-dev+abc+def"}) {
        const auto version = SemVer::parse(text);
        ASSERT_FALSE(version.has_value()) << text;
        EXPECT_EQ(version.error().code, hio::ErrorCode::UsageInvalidArgument);
    }
}

TEST(SemVer, AcceptsSpecExamples) {
    for (const std::string_view text :
         {"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-0.3.7", "1.0.0-x.7.z.92", "1.0.0-x-y-z.--",
          "1.0.0-alpha+001", "1.0.0+20130313144700", "1.0.0-beta+exp.sha.5114f85",
          "1.0.0+21AF26D3----117B344092BD", "4294967295.0.0"}) {
        EXPECT_TRUE(SemVer::parse(text).has_value()) << text;
    }
}

// SemVer 2.0.0 section 11: 1.0.0-alpha < 1.0.0-alpha.1 < 1.0.0-alpha.beta < 1.0.0-beta <
// 1.0.0-beta.2 < 1.0.0-beta.11 < 1.0.0-rc.1 < 1.0.0, and 1.0.0 < 2.0.0 < 2.1.0 < 2.1.1.
TEST(SemVer, PrecedenceTableFromTheSpec) {
    const std::vector<std::string_view> ordered{"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta",
                                                "1.0.0-beta",  "1.0.0-beta.2",  "1.0.0-beta.11",
                                                "1.0.0-rc.1",  "1.0.0",         "2.0.0",
                                                "2.1.0",       "2.1.1",         "10.0.0"};
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        for (std::size_t j = 0; j < ordered.size(); ++j) {
            const SemVer a = v(ordered.at(i));
            const SemVer b = v(ordered.at(j));
            EXPECT_EQ(a < b, i < j) << ordered.at(i) << " vs " << ordered.at(j);
            EXPECT_EQ(a == b, i == j) << ordered.at(i) << " vs " << ordered.at(j);
        }
    }
}

TEST(SemVer, BuildMetadataIsIgnored) {
    EXPECT_EQ(v("2.0.0+abc"), v("2.0.0+def"));
    EXPECT_EQ(v("2.0.0-rc.1+abc"), v("v2.0.0-rc.1"));
    EXPECT_FALSE(v("2.0.0+zzz") < v("2.0.0+aaa"));
}

TEST(SemVer, NumericIdentifiersCompareByValue) {
    EXPECT_LT(v("2.0.0-rc.9"), v("2.0.0-rc.10"));
    EXPECT_LT(v("2.0.0-99999999999999999999"), v("2.0.0-100000000000000000000"));
    EXPECT_LT(v("2.0.0-999"), v("2.0.0-a")); // numeric < alphanumeric
}

TEST(SemVer, DevelopmentBuildsCompareByTheirNumericPart) {
    const SemVer dev = v("2.0.1-dev.5+abc1234");
    EXPECT_TRUE(hio::update::isDevelopmentBuild(dev));
    EXPECT_EQ(hio::update::comparableCurrent(dev), v("2.0.1"));
    EXPECT_TRUE(hio::update::isDevelopmentBuild(v("2.0.0-rc.1.dev.5+abc")));
    EXPECT_TRUE(hio::update::isDevelopmentBuild(v("2.0.0-dev+abc")));
    EXPECT_FALSE(hio::update::isDevelopmentBuild(v("2.0.0-rc.1")));
    EXPECT_EQ(hio::update::comparableCurrent(v("2.0.0-rc.1")), v("2.0.0-rc.1"));
    EXPECT_FALSE(hio::update::isDevelopmentBuild(v("2.0.0-devel")));
}

} // namespace
