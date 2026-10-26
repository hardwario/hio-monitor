#!/bin/bash
# Checks the Linux packages in the Ubuntu 22.04 build container (plan/features/0015 C1-C3):
#   - glibc symbol ceiling: no ELF file of the AppImage or the .deb needs more than GLIBC_2.35
#     (ADR-0121: Ubuntu 22.04);
#   - lintian on the .deb (errors and warnings fail; overrides only for /opt and the private Qt);
#   - desktop-file-validate and appstreamcli validate --no-net on the shipped desktop file and
#     AppStream metainfo;
#   - the AppImage runs the CLI: `<AppImage> hio --version --json` reports "package": "appimage"
#     (with --appimage-extract-and-run, no FUSE in containers).
#
#   packaging/linux/check-packages.sh <package-dir>
set -euo pipefail
dir="$(cd "${1:?package directory}" && pwd)"
deb="$(ls "$dir"/hardwario-monitor_*_amd64.deb)"
appimage="$(ls "$dir"/hio-monitor-*-x86_64.AppImage)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

echo "== Unpacking"
dpkg-deb -x "$deb" "$work/deb"
(cd "$work" && "$appimage" --appimage-extract >/dev/null && mv squashfs-root appimage)

echo "== glibc symbol versions (ceiling GLIBC_2.35)"
max="$(find "$work/deb" "$work/appimage" -type f \( -perm -u+x -o -name '*.so*' \) -print0 \
    | xargs -0 -r file | grep -E 'ELF.*(executable|shared object)' | cut -d: -f1 \
    | xargs -r objdump -T 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -n 1)"
echo "highest: $max"
if [ "$(printf '%s\nGLIBC_2.35\n' "$max" | sort -V | tail -n 1)" != "GLIBC_2.35" ]; then
    echo "An ELF file needs $max (more than GLIBC_2.35)" >&2
    exit 1
fi

echo "== lintian"
lintian --no-tag-display-limit --fail-on error,warning "$deb"

echo "== desktop file and AppStream metainfo"
desktop-file-validate "$work/deb/usr/share/applications/com.hardwario.monitor.desktop"
desktop-file-validate "$work/appimage/usr/share/applications/com.hardwario.monitor.desktop"
appstreamcli validate --no-net "$work/deb/usr/share/metainfo/com.hardwario.monitor.metainfo.xml"

echo "== AppImage CLI dispatch"
json="$(APPIMAGE_EXTRACT_AND_RUN=1 "$appimage" hio --version --json)"
echo "$json"
python3 -I -c 'import json,sys; d=json.loads(sys.argv[1])["data"]; sys.exit(0 if d["package"] == "appimage" else 1)' "$json"
ln -s "$appimage" "$work/hio"
APPIMAGE_EXTRACT_AND_RUN=1 "$work/hio" --version
echo "All package checks passed."
