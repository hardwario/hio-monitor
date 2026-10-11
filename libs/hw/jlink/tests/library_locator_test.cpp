#include "hio/core/error.h"
#include "hio/core/settings.h"
#include "hio/hw/jlink/library_locator.h"

#include <QTemporaryDir>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace {

namespace fs = std::filesystem;
using hio::ErrorCode;
using hio::jlink::LibrarySource;
using hio::jlink::LocatorOptions;

#ifdef _WIN32
constexpr const char* libraryName = "JLink_x64.dll";
#elifdef __APPLE__
constexpr const char* libraryName = "libjlinkarm.dylib";
#else
constexpr const char* libraryName = "libjlinkarm.so";
#endif

class LibraryLocator : public ::testing::Test {
protected:
    [[nodiscard]] fs::path root() const { return {m_dir.path().toStdString()}; }

    // Creates <root>/<relative> with some content (a fake library file).
    fs::path touch(const fs::path& relative) const { // NOLINT(modernize-use-nodiscard)
        const fs::path path = root() / relative;
        fs::create_directories(path.parent_path());
        std::ofstream(path) << "not really a library";
        return path;
    }

    [[nodiscard]] LocatorOptions discoveryOnly() const {
        LocatorOptions options;
        options.searchRoots = {root() / "SEGGER"};
        return options;
    }

private:
    QTemporaryDir m_dir;
};

TEST_F(LibraryLocator, OptionWinsOverEnvironmentAndSetting) {
    const fs::path option = touch("option/lib");
    const fs::path environment = touch("env/lib");
    const fs::path setting = touch("setting/lib");
    LocatorOptions options = discoveryOnly();
    options.option = option;
    options.environment = environment;
    options.setting = setting;
    auto location = hio::jlink::locateLibrary(options);
    ASSERT_TRUE(location) << location.error().detail;
    EXPECT_EQ(location->path, option);
    EXPECT_EQ(location->source, LibrarySource::Option);

    options.option.reset();
    location = hio::jlink::locateLibrary(options);
    ASSERT_TRUE(location);
    EXPECT_EQ(location->path, environment);
    EXPECT_EQ(location->source, LibrarySource::Environment);

    options.environment.reset();
    location = hio::jlink::locateLibrary(options);
    ASSERT_TRUE(location);
    EXPECT_EQ(location->path, setting);
    EXPECT_EQ(location->source, LibrarySource::Setting);
}

TEST_F(LibraryLocator, MissingOverrideIsAnErrorEvenWhenDiscoveryWouldFindOne) {
    touch(fs::path("SEGGER/JLink") / libraryName);
    LocatorOptions options = discoveryOnly();
    options.option = root() / "nonexistent";
    const auto location = hio::jlink::locateLibrary(options);
    ASSERT_FALSE(location);
    EXPECT_EQ(location.error().code, ErrorCode::DependencyJLinkNotFound);
    EXPECT_NE(location.error().detail.find("nonexistent"), std::string::npos);
    EXPECT_NE(location.error().detail.find("--jlink-library"), std::string::npos);
    EXPECT_EQ(location.error().wizard, hio::WizardId::FirstRunSetup);
}

TEST_F(LibraryLocator, OverrideDirectoryIsSearched) {
    const fs::path library = touch(fs::path("custom") / libraryName);
    LocatorOptions options = discoveryOnly();
    options.setting = root() / "custom";
    const auto location = hio::jlink::locateLibrary(options);
    ASSERT_TRUE(location);
    EXPECT_EQ(location->path, library);
}

TEST_F(LibraryLocator, DiscoveryPicksTheHighestVersion) {
    touch(fs::path("SEGGER/JLink_V794k") / libraryName);
    const fs::path newest = touch(fs::path("SEGGER/JLink_V812a") / libraryName);
    touch(fs::path("SEGGER/JLink_V810") / libraryName);
    touch(fs::path("SEGGER/Other") / libraryName); // not a J-Link directory
    const auto location = hio::jlink::locateLibrary(discoveryOnly());
    ASSERT_TRUE(location) << location.error().detail;
    EXPECT_EQ(location->path, newest);
    EXPECT_EQ(location->versionFromName, 81201U);
    EXPECT_EQ(location->source, LibrarySource::Discovered);
}

TEST_F(LibraryLocator, InstallPathsAreSearched) {
    const fs::path library = touch(fs::path("Program Files/SEGGER/JLink") / libraryName);
    LocatorOptions options;
    options.installPaths = {library.parent_path()};
    const auto location = hio::jlink::locateLibrary(options);
    ASSERT_TRUE(location);
    EXPECT_EQ(location->path, library);
}

#ifndef _WIN32
TEST_F(LibraryLocator, SymlinkedNamesCountOnceWithTheirHighestVersion) {
    const fs::path real = touch("SEGGER/JLink/libjlinkarm.so.8.10.1");
    fs::create_symlink(real.filename(), root() / "SEGGER/JLink/libjlinkarm.so.8");
    fs::create_symlink("libjlinkarm.so.8", root() / "SEGGER/JLink/libjlinkarm.so");
    touch("SEGGER/JLink_V794k/libjlinkarm.so.7.94.11");
    fs::create_symlink("missing", root() / "SEGGER/JLink/libjlinkarm.so.9"); // dangling
    const auto location = hio::jlink::locateLibrary(discoveryOnly());
    ASSERT_TRUE(location) << location.error().detail;
    EXPECT_EQ(location->versionFromName, 81001U);
    EXPECT_EQ(fs::canonical(location->path), fs::canonical(real));
}
#endif

TEST_F(LibraryLocator, NothingFoundListsTheSearchedPaths) {
    const auto location = hio::jlink::locateLibrary(discoveryOnly());
    ASSERT_FALSE(location);
    EXPECT_EQ(location.error().code, ErrorCode::DependencyJLinkNotFound);
    EXPECT_NE(location.error().detail.find("SEGGER"), std::string::npos) << location.error().detail;
}

TEST_F(LibraryLocator, DefaultOptionsReadEnvironmentAndSetting) {
    auto settings = hio::Settings::openFile(root() / "settings.ini");
    ASSERT_TRUE(settings);
    ASSERT_TRUE(settings->set(hio::jlink::libraryPathSetting(), fs::path("/from/setting")));
    qputenv("HIO_JLINK_LIBRARY", "/from/env");
    const LocatorOptions options = hio::jlink::defaultLocatorOptions(std::nullopt, &*settings);
    qunsetenv("HIO_JLINK_LIBRARY");
    EXPECT_FALSE(options.option.has_value());
    EXPECT_EQ(options.environment, fs::path("/from/env"));
    EXPECT_EQ(options.setting, fs::path("/from/setting"));
    EXPECT_FALSE(options.searchRoots.empty());

    const LocatorOptions empty = hio::jlink::defaultLocatorOptions(fs::path(), nullptr);
    EXPECT_FALSE(empty.option.has_value()); // an empty option means "not given"
    EXPECT_FALSE(empty.setting.has_value());
}

} // namespace
