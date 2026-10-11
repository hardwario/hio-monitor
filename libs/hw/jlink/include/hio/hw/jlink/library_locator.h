#pragma once

// Finds the JLinkARM library of the user's SEGGER J-Link Software installation (ADR-0006).
//
// Precedence: explicit option (CLI --jlink-library) > environment HIO_JLINK_LIBRARY > setting
// jlink/libraryPath > auto-discovery. An override that does not exist is an error (it is never
// silently replaced by a discovered library).
//
// Auto-discovery (plan/features/0002-m1-jlink-rtt.md):
//   Linux    /opt/SEGGER/JLink/libjlinkarm.so*  and  /opt/SEGGER/JLink_V*/libjlinkarm.so*
//   macOS    /Applications/SEGGER/JLink/libjlinkarm.dylib  and  /Applications/SEGGER/JLink_V*/
//   Windows  HKCU|HKLM\SOFTWARE\SEGGER\J-Link\InstallPath  and
//   %ProgramFiles%\SEGGER\JLink*\JLink_x64.dll
// Among the candidates the highest version (from the file or directory name) wins.

#include "hio/core/result.h"
#include "hio/core/settings.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace hio::jlink {

enum class LibrarySource : std::uint8_t {
    Option,      // --jlink-library
    Environment, // HIO_JLINK_LIBRARY
    Setting,     // jlink/libraryPath
    Discovered,  // auto-discovery
};

struct LibraryLocation {
    std::filesystem::path path;
    // Version guessed from the file or directory name (0 = unknown). The real version is read
    // from the library when it is loaded (JLinkLibrary::libraryVersion).
    std::uint32_t versionFromName = 0;
    LibrarySource source = LibrarySource::Discovered;
};

struct LocatorOptions {
    std::optional<std::filesystem::path> option;      // CLI --jlink-library
    std::optional<std::filesystem::path> environment; // HIO_JLINK_LIBRARY
    std::optional<std::filesystem::path> setting;     // jlink/libraryPath
    // Where auto-discovery looks (directories that contain the SEGGER installations, e.g.
    // /opt/SEGGER) and, on Windows, the install paths read from the registry. Defaults:
    // defaultSearchRoots() / registryInstallPaths(); tests pass their own.
    std::vector<std::filesystem::path> searchRoots;
    std::vector<std::filesystem::path> installPaths;
};

// The setting key of the library override (ADR-0025): "jlink/libraryPath", empty = auto.
[[nodiscard]] SettingKey<std::filesystem::path> libraryPathSetting();

// The name of the environment variable with the library override.
inline constexpr std::string_view libraryEnvironmentVariable = "HIO_JLINK_LIBRARY";

// LocatorOptions with the environment, the setting (when `settings` is given) and the default
// search locations of this OS filled in. `option` is the CLI value.
[[nodiscard]] LocatorOptions defaultLocatorOptions(std::optional<std::filesystem::path> option,
                                                   const Settings* settings);

[[nodiscard]] std::vector<std::filesystem::path> defaultSearchRoots();
// Windows: InstallPath values under HKCU and HKLM\SOFTWARE\SEGGER\J-Link; empty elsewhere.
[[nodiscard]] std::vector<std::filesystem::path> registryInstallPaths();

// Errors: dependency.jlink_not_found (searched paths in `detail`).
[[nodiscard]] Result<LibraryLocation> locateLibrary(const LocatorOptions& options);

// Version from a library file name or its directory ("libjlinkarm.so.8.10.1", "JLink_V810a");
// 0 when none can be read.
[[nodiscard]] std::uint32_t versionFromPath(const std::filesystem::path& path);

} // namespace hio::jlink
