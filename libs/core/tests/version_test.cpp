#include "hwio/core/app_identity.h"
#include "hwio/core/json.h"
#include "hwio/core/version.h"
#include "hwio/core/version_generated.h"

#include <QCoreApplication>
#include <QString>

#include <gtest/gtest.h>

#include <regex>
#include <string>

namespace {

TEST(BuildInfo, VersionIsSemver) {
    const hwio::BuildInfo info = hwio::buildInfo();
    // X.Y.Z[-prerelease][+build]
    const std::regex semver(R"(^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?(\+[0-9A-Za-z.]+)?$)");
    EXPECT_TRUE(std::regex_match(std::string(info.version), semver)) << info.version;
    EXPECT_EQ(info.version, hwio::generated::version);
    EXPECT_TRUE(info.version.starts_with(hwio::generated::versionNumeric) ||
                info.version.find('-') != std::string_view::npos);
}

TEST(BuildInfo, FieldsAreFilled) {
    const hwio::BuildInfo info = hwio::buildInfo();
    EXPECT_FALSE(info.gitHash.empty());
    EXPECT_FALSE(info.compiler.empty());
    EXPECT_TRUE(info.compiler.find("Clang") != std::string_view::npos) << info.compiler;
    EXPECT_TRUE(info.qtRuntime.starts_with("6.")) << info.qtRuntime;
    EXPECT_TRUE(info.qtCompiled.starts_with("6.")) << info.qtCompiled;
}

TEST(AppIdentity, SetsQtNames) {
    hwio::initApplicationIdentity();
    EXPECT_EQ(QCoreApplication::organizationName(), QStringLiteral("HARDWARIO"));
    EXPECT_EQ(QCoreApplication::organizationDomain(), QStringLiteral("hardwario.com"));
    EXPECT_EQ(QCoreApplication::applicationName(), QStringLiteral("Monitor"));
    EXPECT_EQ(QCoreApplication::applicationVersion(), hwio::toQString(hwio::buildInfo().version));
}

} // namespace
