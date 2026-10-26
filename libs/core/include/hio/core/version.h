#pragma once

// Build information. The version comes from git tags (cmake/HioVersion.cmake).

#include <string_view>

namespace hio {

struct BuildInfo {
    std::string_view version;    // semantic version, e.g. "2.0.0-dev+abc1234"
    std::string_view gitHash;    // short commit hash or "unknown"
    std::string_view compiler;   // e.g. "Clang 21.1.8"
    std::string_view qtRuntime;  // Qt version loaded at runtime (qVersion())
    std::string_view qtCompiled; // Qt version the build was compiled against
    // How this build is distributed (plan/features/0015): "appimage", "deb", "macos-bundle",
    // "msi" or "dev" (CMake cache variable HIO_PACKAGE_KIND).
    std::string_view packageKind;
};

[[nodiscard]] BuildInfo buildInfo();

} // namespace hio
