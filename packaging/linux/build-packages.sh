#!/bin/bash
# Builds the Linux packages in the Ubuntu 22.04 build container (plan/features/0015 C1-C3,
# ADR-0121: one build feeds both formats):
#   1. preset linux-package (Release, HIO_PACKAGE_KIND=deb): build, `cpack` -> .deb
#   2. the same build tree reconfigured with HIO_PACKAGE_KIND=appimage (only the package kind
#      changes: version.cpp is recompiled, the executables relinked) -> AppImage
#
#   packaging/linux/build-packages.sh [<output-dir>]      (default: build/packages)
#
# Local use (packaging/README.md): run it inside the container with the source tree mounted.
set -euo pipefail
src="$(cd "$(dirname "$0")/../.." && pwd)"
out="${1:-$src/build/packages}"
mkdir -p "$out"
cd "$src"

cmake --preset linux-package -DHIO_PACKAGE_KIND=deb
cmake --build --preset linux-package
(cd build/linux-package && cpack -G DEB -B "$out")
rm -rf "$out/_CPack_Packages"

cmake --preset linux-package -DHIO_PACKAGE_KIND=appimage
cmake --build --preset linux-package
packaging/linux/build-appimage.sh build/linux-package "$out"
ls -l "$out"
