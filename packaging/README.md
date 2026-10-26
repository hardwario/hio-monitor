# Packaging (plan/features/0015, ADR-0115 … ADR-0126, ADR-0177)

How HARDWARIO Monitor v2 is installed, packaged and signed. The release procedure is in
[RELEASE.md](RELEASE.md).

| Package | OS | Built by | Signed |
|---|---|---|---|
| `hio-monitor-<v>-x86_64.AppImage` | Ubuntu 22.04+ and other glibc ≥ 2.35 distributions | `packaging/linux/build-packages.sh` (container) | no (SHA256SUMS + provenance) |
| `hardwario-monitor_<v>_amd64.deb` | Ubuntu 24.04+ / Debian 13+ | same build tree, CPack DEB | no (SHA256SUMS + provenance) |
| `hio-monitor-<v>-macos-universal.dmg` | macOS 13+ (arm64 + x86_64) | `macos-package` preset, `packaging/macos/sign-and-notarize.sh` | Developer ID, notarized, stapled |
| `hio-monitor-<v>-windows-x64.msi` | Windows 10 22H2+ x64 | `windows-package` preset, CPack WIX (WiX ≥ 4) | Azure Trusted Signing |

`<v>` is the version from the git tag (`cmake/HioVersion.cmake`; build metadata `+` is written as `.` in file
names). No package contains SEGGER J-Link Software, its USB driver or nRF Util (ADR-0006, ADR-0016).

## Layout

```
cmake/HioPackaging.cmake      install rules per OS, HIO_PACKAGE_KIND, CPack, installed-tree CTest
cmake/HioDeploy.cmake.in      Qt deployment at install time (Linux: explicit plugin / QML module lists;
                              macOS: macdeployqt; Windows: windeployqt), Czech Qt catalogs
cmake/HioInstalledTreeTest.cmake  CTest Packaging.InstalledTreeSelfTest
packaging/common/icons/       the v1 icon (hio-monitor.icns / .ico from branch main) + hicolor PNGs (extract.sh)
packaging/common/licenses/    third-party licence texts; licenses.cmake installs them + generated notices
packaging/linux/              Containerfile, build-packages.sh, build-appimage.sh, check-packages.sh,
                              smoke-clean.sh, desktop file, AppStream metainfo, udev rule, deb/, appimage/AppRun
packaging/macos/              entitlements.plist, sign-and-notarize.sh, dmg/background.png
packaging/windows/            version.rc.in (icon + VERSIONINFO), wix/ (CPack config, template, features), sign.ps1
```

`HIO_PACKAGE_KIND` (`dev` | `appimage` | `deb` | `macos-bundle` | `msi`) is compiled into the binaries
(`hio --version --json` → `"package"`, Settings › About, the diagnostic report). Normal builds are `dev`; the
`*-package` presets set the kind (Release, no tests, debug info for the symbol archives).

## Installed tree and self-test

`cmake --install <build> --prefix <dir>` gives a self-contained tree on every OS (Qt shared and replaceable,
ADR-0119). The CTest `Packaging.InstalledTreeSelfTest` (all presets with tests) installs into
`<build>/installed-tree`, then runs `hio --version --json` and `hio-monitor --self-test` (hidden option: loads
the main QML and exits 0) with `QT_QPA_PLATFORM=offscreen` and the Qt search paths cleared.

## Linux (ADR-0118, ADR-0121)

The packages are built in an Ubuntu 22.04 container (glibc 2.35, LLVM 21, **g++-13 / libstdc++ 13** — the
regression check of ADR-0177, Qt 6.8.3 from aqtinstall, CMake from Kitware, linuxdeploy / appimagetool / type-2
runtime with SHA-256 checks). Locally (Docker or Podman):

```sh
docker build -t hio-monitor-build:22.04 -f packaging/linux/Containerfile packaging/linux
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:$PWD" -w "$PWD" hio-monitor-build:22.04 \
    packaging/linux/build-packages.sh build/packages        # .deb, then AppImage (same build tree)
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:$PWD" -w "$PWD" hio-monitor-build:22.04 \
    packaging/linux/check-packages.sh build/packages        # glibc ceiling, lintian, desktop, AppStream
packaging/linux/smoke-clean.sh build/packages               # clean ubuntu:22.04 / 24.04 containers
```

In a git worktree the main repository's `.git` directory must be mounted too (the version comes from
`git describe`). The full test suite in the container: `cmake --preset linux-debug -B build/c2204-debug &&
cmake --build build/c2204-debug && ctest --test-dir build/c2204-debug` (with `QT_QPA_PLATFORM=offscreen`).

- **.deb:** `/opt/hardwario-monitor/{bin,lib,plugins,qml,translations,licenses}` (private Qt, `$ORIGIN/../lib`),
  links `/usr/bin/hio-monitor` and `/usr/bin/hio`, desktop file, metainfo, hicolor icons, man pages, the J-Link
  udev rule `/usr/lib/udev/rules.d/60-hardwario-jlink.rules` (postinst/postrm reload udev; errors are ignored in
  containers). Dependencies from `dpkg-shlibdeps` (incl. `libstdc++6 (>= 13.1)`, `libxcb-cursor0`) plus `bluez`.
  lintian must be clean; overrides only for `/opt` and the private Qt (`deb/lintian-overrides`).
- **AppImage:** AppDir from the install tree, linuxdeploy without its Qt plugin for the remaining libraries
  (`libdbus-1` stays bundled), libstdc++ / libgcc_s in `usr/optional` with the checkrt in `AppRun` (used only when
  newer than the system's), static type-2 runtime, no update information (ADR-0116). `AppRun` starts `hio` when
  called through a link named `hio` or with `hio` as the first argument, otherwise the GUI. Without FUSE:
  `--appimage-extract-and-run` or `APPIMAGE_EXTRACT_AND_RUN=1`.

## macOS (ADR-0120, ADR-0126)

```sh
cmake --preset macos-package && cmake --build --preset macos-package
cmake --install build/macos-package --prefix build/macos-install          # macdeployqt
packaging/macos/sign-and-notarize.sh sign "build/macos-install/HARDWARIO Monitor.app" -    # ad hoc
packaging/macos/sign-and-notarize.sh dmg  "build/macos-install/HARDWARIO Monitor.app" out.dmg -
```

Signing is inside-out (frameworks, plugins and QML plugins, then `Contents/MacOS/hio`, then the bundle), with the
hardened runtime, `--timestamp` (not ad hoc) and `entitlements.plist` (only
`com.apple.security.cs.disable-library-validation`, so that SEGGER's JLinkARM library and its copies load). The
`verify` step checks the strict signature, the hardened runtime and the entitlements of both binaries and that
the Bluetooth usage descriptions survived (ADR-0086). The DMG is made with create-dmg (pinned release archive,
SHA-256 checked) and, in the release workflow only, notarized with `xcrun notarytool` (App Store Connect API
key) and stapled.

## Windows (ADR-0117, ADR-0120)

From a VS developer prompt (the MSVC runtime DLLs are copied app-local from `VCToolsRedistDir`), with CMake ≥ 3.30
and WiX ≥ 4 (`dotnet tool install --global wix --version 5.0.2` and
`wix extension add -g WixToolset.UI.wixext/5.0.2`):

```powershell
cmake --preset windows-package; cmake --build --preset windows-package
packaging/windows/sign.ps1 -DryRun -Files build/windows-package/app/gui/hio-monitor.exe,build/windows-package/app/cli/hio.exe
cpack --preset windows-package -B build/packages
wix msi validate build/packages/hio-monitor-<v>-windows-x64.msi
```

The MSI is per-machine (`%ProgramFiles%\HARDWARIO\Monitor`), with a fixed UpgradeCode (major upgrades; a
downgrade shows "A newer version of HARDWARIO Monitor is already installed."), the features "Add hio to PATH"
(on) and "Desktop shortcut" (off; `msiexec /i <msi> /qn ADDDESKTOPSHORTCUT=1`) and a Start-menu shortcut with the
AppUserModelID `com.hardwario.monitor`. The executables are signed after the build and before CPack, the MSI
after CPack; `signtool verify /pa` checks all three.

## CI (ADR-0125)

- `ci.yml` (pull requests, `v2-dev`): `package-linux`, `package-macos`, `package-windows` build unsigned packages
  after the normal jobs, run the checks above and upload them as artifacts for 7 days.
- `release.yml` (tags `v2.*.*`): gate (version = tag, 0 unfinished Czech translations, `HIO_TEST_OVERRIDES=OFF`,
  the whole `ci.yml` through `workflow_call`), signed builds, symbol archives, `SHA256SUMS`, build provenance
  attestations and a **draft** release. See [RELEASE.md](RELEASE.md).

## Secrets (environment `release`, required reviewers)

| Secret | Use | Rotation |
|---|---|---|
| `MACOS_CERT_P12_BASE64` | Developer ID Application certificate + key (.p12, base64) | before expiry (5 years); export a new .p12 from the Apple Developer account, replace both secrets |
| `MACOS_CERT_PASSWORD` | password of the .p12 | with the certificate |
| `MACOS_NOTARY_KEY_P8` | App Store Connect API key (.p8 content) for notarytool | revoke and create a new key in App Store Connect › Users and Access › Keys when a maintainer leaves or yearly |
| `MACOS_NOTARY_KEY_ID` | its key id | with the key |
| `MACOS_NOTARY_ISSUER` | the issuer id of the team | does not change |
| `AZURE_CLIENT_ID` | app registration with a federated credential for this repository's `release` environment (OIDC, no client secret) | no secret to rotate; review the federated credential subject when the repository or environment changes |
| `AZURE_TENANT_ID` | Azure tenant | — |
| `AZURE_SUBSCRIPTION_ID` | subscription of the Trusted Signing account | — |
| `TRUSTED_SIGNING_ACCOUNT` | Trusted Signing (Artifact Signing) account name | — |
| `TRUSTED_SIGNING_PROFILE` | certificate profile name | Microsoft renews the short-lived certificates itself |
| `TRUSTED_SIGNING_ENDPOINT` | regional endpoint, e.g. `https://weu.codesigning.azure.net/` | — |

PR builds never see these secrets and are never signed.

## Troubleshooting

- **Container build fails at apt.llvm.org / the PPA:** the LLVM 21 and g++-13 packages must stay available for
  jammy (risk in plan 0015); pin a mirror or rebuild the cached image when they move.
- **`hio-monitor --self-test` fails in the installed tree:** run it with `QT_DEBUG_PLUGINS=1` and check
  `qml/` against `HIO_LINUX_QML_MODULES` / `HIO_LINUX_QT_PLUGINS` (cmake/HioPackaging.cmake).
- **AppImage does not start without FUSE:** `--appimage-extract-and-run`.
- **notarytool: Invalid:** `xcrun notarytool log <submission id> --key … --key-id … --issuer …` lists the files
  (usually an unsigned Mach-O or a missing hardened runtime); the job prints the log in notarytool.log.
- **WiX errors about extensions:** the WiX extension version must match the WiX tool version (`WIX_VERSION`).
