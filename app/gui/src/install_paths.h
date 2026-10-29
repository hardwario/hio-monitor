#pragma once

// Paths of this installation for Settings > About (plan/features/0016 C2, ADR-0119, ADR-0122),
// by the package kind (BuildInfo::packageKind, plan 0015):
//   licences   deb / appimage: <bin>/../licenses; macos-bundle: Contents/Resources/licenses;
//              msi: <install dir>/licenses; dev: <bin>/../licenses when installed with
//              `cmake --install`, otherwise the source and build tree files.
//   CLI        deb: /usr/bin/hwio; appimage: "<$APPIMAGE>" hwio; macos-bundle:
//              Contents/MacOS/hwio; msi: <install dir>\hwio.exe (on PATH); dev: the build tree's
//              hwio.
// The layout is the one of cmake/HWioPackaging.cmake (install rules). GUI thread
// (QCoreApplication).

#include <QString>
#include <QUrl>

namespace hwio::gui {

// The licence file `fileName` ("LICENSE", "Qt-LGPL-3.0.txt", ...) of this installation (a local
// file URL; empty when it does not exist).
[[nodiscard]] QUrl licenceFile(const QString& fileName);

// The command that runs the CLI of this installation (a path, or `"<AppImage>" hwio`).
[[nodiscard]] QString cliCommand();

// macOS: the command that links the CLI into /usr/local/bin ("" elsewhere and for dev builds).
[[nodiscard]] QString cliSymlinkCommand();

// The CLI is on PATH (deb: /usr/bin, msi: the installer's PATH entry).
[[nodiscard]] bool cliOnPath();

} // namespace hwio::gui
