#!/bin/bash
# Smoke tests of the Linux packages in clean containers (plan/features/0015 C2-C3); needs docker.
#   - AppImage on a clean Ubuntu 22.04 (the baseline, ADR-0121): `hio --version --json` reports
#     "package": "appimage" (also through a link named hio), `hio doctor` runs, and the GUI's
#     --self-test passes offscreen with only the basic GL/font libraries installed.
#   - .deb on a clean Ubuntu 24.04: apt installs it with its dependencies; hio and hio-monitor are
#     on PATH, the udev rule is installed, the GUI self-test passes; apt removes everything again.
#
#   packaging/linux/smoke-clean.sh <package-dir>
set -euo pipefail
dir="$(cd "${1:?package directory}" && pwd)"
appimage="$(basename "$(ls "$dir"/hio-monitor-*-x86_64.AppImage)")"
deb="$(basename "$(ls "$dir"/hardwario-monitor_*_amd64.deb)")"

echo "== AppImage on ubuntu:22.04"
docker run --rm -v "$dir:/packages:ro" -e APPIMAGE_EXTRACT_AND_RUN=1 -e "APP=/packages/$appimage" \
    ubuntu:22.04@sha256:0a9887931b0e38af2e3487ed39e29436b0535c4f3f52e57745baaf79d1898956 bash -euc '
    cd /tmp
    "$APP" hio --version --json | tee version.json
    grep -q "\"package\":\"appimage\"" version.json
    ln -s "$APP" /tmp/hio && /tmp/hio --version
    HIO_APPRUN_DEBUG=1 "$APP" hio --version 2>&1 | grep -q "use_bundled=1"   # libstdc++ 12 on 22.04
    "$APP" hio doctor >/dev/null || true                                       # no J-Link here
    export DEBIAN_FRONTEND=noninteractive
    apt-get update >/dev/null
    apt-get install -y --no-install-recommends libgl1 libegl1 libopengl0 libfontconfig1 \
        libx11-6 libfreetype6 >/dev/null
    mkdir -p -m 700 /tmp/runtime
    XDG_RUNTIME_DIR=/tmp/runtime QT_QPA_PLATFORM=offscreen HIO_NO_CRASH_HANDLER=1 "$APP" --self-test
'

echo "== .deb on ubuntu:24.04"
docker run --rm -v "$dir:/packages:ro" -e "DEB=/packages/$deb" ubuntu:24.04 bash -euc '
    export DEBIAN_FRONTEND=noninteractive
    apt-get update >/dev/null
    apt-get install -y --no-install-recommends "$DEB" >/dev/null
    command -v hio hio-monitor
    hio --version --json | tee /tmp/version.json
    grep -q "\"package\":\"deb\"" /tmp/version.json
    test -f /usr/lib/udev/rules.d/60-hardwario-jlink.rules
    test -f /usr/share/applications/com.hardwario.monitor.desktop
    test -f /usr/share/icons/hicolor/256x256/apps/com.hardwario.monitor.png
    mkdir -p -m 700 /tmp/runtime
    XDG_RUNTIME_DIR=/tmp/runtime QT_QPA_PLATFORM=offscreen HIO_NO_CRASH_HANDLER=1 hio-monitor --self-test
    apt-get remove -y hardwario-monitor >/dev/null
    test ! -e /usr/lib/udev/rules.d/60-hardwario-jlink.rules
    test ! -e /usr/bin/hio
    test ! -e /opt/hardwario-monitor/bin/hio-monitor
    echo ".deb installed, run and removed"
'
echo "All clean-container smoke tests passed."
