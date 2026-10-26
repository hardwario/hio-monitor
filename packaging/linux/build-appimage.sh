#!/bin/bash
# Builds the AppImage from a build tree configured with HIO_PACKAGE_KIND=appimage (plan/features/0015
# C3, ADR-0118, ADR-0121). Runs in the Ubuntu 22.04 build container (packaging/linux/Containerfile),
# which provides linuxdeploy, appimagetool and the type-2 runtime (pinned, SHA-256 checked).
#
#   packaging/linux/build-appimage.sh <build-dir> <output-dir>
#
# 1. AppDir from the install tree: `cmake --install` with prefix AppDir (AppDir/usr/{bin,lib,plugins,
#    qml,translations,licenses,share}; Qt is deployed by cmake/HioDeploy.cmake.in).
# 2. The build's libstdc++ / libgcc_s into usr/optional (checkrt in AppRun).
# 3. linuxdeploy without its Qt plugin: bundles the remaining non-Qt libraries of the executables,
#    Qt libraries, plugins and QML modules (e.g. libxcb-cursor0, libxkbcommon-x11, libdbus-1 - it
#    must stay bundled, `hio` needs it on a clean Ubuntu 22.04). Libraries every desktop has, or
#    that must match the host (systemd, Kerberos, GnuTLS, ...), are excluded.
# 4. Our AppRun (dispatch GUI / hio, checkrt), then appimagetool with the static type-2 runtime.
#    No update information (ADR-0116: no zsync).
set -euo pipefail

build="$(cd "$1" && pwd)"
out="$(mkdir -p "$2" && cd "$2" && pwd)"
here="$(cd "$(dirname "$0")" && pwd)"
runtime="${HIO_APPIMAGE_RUNTIME:?HIO_APPIMAGE_RUNTIME (the type-2 runtime) is not set}"

kind="$(sed -n 's/^HIO_PACKAGE_KIND:STRING=//p' "$build/CMakeCache.txt")"
if [ "$kind" != "appimage" ]; then
    echo "build-appimage.sh: $build is configured with HIO_PACKAGE_KIND=$kind, not appimage" >&2
    exit 1
fi
version="$("$build/app/cli/hio" --version --json | python3 -I -c 'import json,sys; print(json.load(sys.stdin)["data"]["version"])')"
name="hio-monitor-${version//+/.}-x86_64.AppImage"

appdir="$build/AppDir"
rm -rf "$appdir"
cmake --install "$build" --prefix "$appdir"

# Our executables without symbols (the release keeps them in the symbol archive, ADR-0123);
# linuxdeploy's own strip is not used (NO_STRIP: too old for the toolchain's ELF files).
strip --strip-unneeded "$appdir/usr/bin/hio-monitor" "$appdir/usr/bin/hio"

# checkrt: the build's C++ runtime.
stdcxx="$(readlink -f /usr/lib/x86_64-linux-gnu/libstdc++.so.6)"
mkdir -p "$appdir/usr/optional/libstdc++" "$appdir/usr/optional/libgcc_s"
cp -L "$stdcxx" "$appdir/usr/optional/libstdc++/"
ln -sf "$(basename "$stdcxx")" "$appdir/usr/optional/libstdc++/libstdc++.so.6"
cp -L /lib/x86_64-linux-gnu/libgcc_s.so.1 "$appdir/usr/optional/libgcc_s/"

deps=()
while IFS= read -r -d '' so; do deps+=(--deploy-deps-only "$so"); done \
    < <(find "$appdir/usr/lib" "$appdir/usr/plugins" "$appdir/usr/qml" -name '*.so*' -type f -print0)
export APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1
LD_LIBRARY_PATH="$appdir/usr/lib" linuxdeploy --appdir "$appdir" \
    --executable "$appdir/usr/bin/hio-monitor" --executable "$appdir/usr/bin/hio" \
    "${deps[@]}" \
    --exclude-library 'libsystemd.so*' --exclude-library 'libgnutls.so*' \
    --exclude-library 'libgssapi_krb5.so*' --exclude-library 'libkrb5*.so*' \
    --exclude-library 'libcurl*.so*' --exclude-library 'libproxy.so*' \
    --exclude-library 'libk5crypto.so*' --exclude-library 'libkeyutils.so*' \
    --exclude-library 'libcom_err.so*' --exclude-library 'libgcrypt.so*' \
    --exclude-library 'libgpg-error.so*' --exclude-library 'libcap.so*' \
    --exclude-library 'liblz4.so*' --exclude-library 'liblzma.so*' \
    --desktop-file "$appdir/usr/share/applications/com.hardwario.monitor.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/com.hardwario.monitor.png"
rm -f "$appdir/AppRun"
install -m 755 "$here/appimage/AppRun" "$appdir/AppRun"

rm -f "$out/$name"
ARCH=x86_64 appimagetool --no-appstream --runtime-file "$runtime" "$appdir" "$out/$name"
ls -l "$out/$name"
