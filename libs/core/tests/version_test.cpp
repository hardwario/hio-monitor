#include "hio/core/app_identity.h"
#include "hio/core/json.h"
#include "hio/core/version.h"
#include "hio/core/version_generated.h"

#include <QCoreApplication>
#include <QString>
#include <QStringLiteral>

#include <gtest/gtest.h>

#include <regex>
#include <string>

namespace {

TEST(BuildInfo, VersionIsSemver) {
    const hio::BuildInfo info = hio::buildInfo();
    // X.Y.Z[-prerelease][+build]
    const std::regex semver(R"(^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?(\+[0-9A-Za-z.]+)?$)");
    EXPECT_TRUE(std::regex_match(std::string(info.version), semver)) << info.version;
    EXPECT_EQ(info.version, hio::generated::version);
    EXPECT_TRUE(info.version.starts_with(hio::generated::versionNumeric) ||
                info.version.find('-') != std::string_view::npos);
}

TEST(BuildInfo, FieldsAreFilled) {
    const hio::BuildInfo info = hio::buildInfo();
    EXPECT_FALSE(info.gitHash.empty());
    EXPECT_FALSE(info.compiler.empty());
    EXPECT_TRUE(info.compiler.find("Clang") != std::string_view::npos) << info.compiler;
    EXPECT_TRUE(info.qtRuntime.starts_with("6.")) << info.qtRuntime;
    EXPECT_TRUE(info.qtCompiled.starts_with("6.")) << info.qtCompiled;
}

TEST(AppIdentity, SetsQtNames) {
    hio::initApplicationIdentity();
    EXPECT_EQ(QCoreApplication::organizationName(), QStringLiteral("HARDWARIO"));
    EXPECT_EQ(QCoreApplication::organizationDomain(), QStringLiteral("hardwario.com"));
    EXPECT_EQ(QCoreApplication::applicationName(), QStringLiteral("Monitor"));
    EXPECT_EQ(QCoreApplication::applicationVersion(), hio::toQString(hio::buildInfo().version));
}

} // namespace
