# 0015 — M7 packaging and release pipeline: AppImage, .deb, DMG, MSI, signing, crash dumps

- **Status:** In progress (approved 2026-10-26, with ADR-0177; implementation started 2026-10-26)
- **Milestone:** M7 (plan 1 of 2, ADR-0115; [0016](0016-m7-update-check.md) is the update check)
- **Author:** Matej Strnad (with Claude), 2026-10-19
- **Related ADRs:** ADR-0002, ADR-0006, ADR-0011, ADR-0012, ADR-0018, ADR-0020, ADR-0021, ADR-0022, ADR-0025,
  ADR-0027, ADR-0030, ADR-0048, ADR-0058, ADR-0071, ADR-0072, ADR-0073, ADR-0075, ADR-0086, ADR-0106, ADR-0108,
  ADR-0115, ADR-0117–ADR-0126, ADR-0177
- **Depends on:** 0001 (M0) implemented. Packages are meaningful once M3 (GUI) is merged; the BLE-related checks
  (Info.plist under hardened runtime, `bluez` dependency) need 0011. Phases A–E can start before M4–M6 are merged;
  the release candidate (phase F) is built from the integrated `v2-dev`.

## Context

Nothing in v2 can be installed yet: there are no `install()` rules, no deploy step, no icons or desktop metadata, no
packaging directory and no tag-triggered CI (`.github/workflows/ci.yml` runs on PRs and `v2-dev` only). Many earlier
decisions were explicitly left to this plan (Qt translations, licence texts, udev rule, glibc baseline, hardened
runtime with the Bluetooth plists, J-Link copies vs signing and antivirus, single instance on Windows, CLI path).

The old app (branch `main`) built an AppImage (linuxdeploy), a signed + notarized universal DMG (macdeployqt, J-Link
and nrfjprog dylibs bundled) and an unsigned Qt-Installer-Framework `.exe` that also ran the SEGGER driver installer.
It built only after a release was created by hand, with `sed`-patched versions. v2 bundles no vendor software
(ADR-0006, ADR-0016) and takes its version from git tags (`cmake/HioVersion.cmake`).

## Goals / Non-goals

**Goals**
1. One CMake install/deploy path per OS producing a self-contained tree (Qt shared and replaceable, Qt `cs`
   translations, licence notices), reused by every package format.
2. Packages (ADR-0118, ADR-0117, ADR-0124): Linux x86_64 **AppImage** (Ubuntu 22.04+) and **.deb** (Ubuntu 24.04+ /
   Debian 13+, private Qt under `/opt`, udev rule), macOS **universal DMG** (signed, notarized, stapled, hardened
   runtime), Windows x64 **per-machine MSI** (WiX, signed, PATH).
3. `hio` ships with the GUI in every package and is on PATH (ADR-0122).
4. Release workflow on `v2.*` tags: gates, signing, `SHA256SUMS`, provenance attestations, symbol archives, draft
   GitHub release (ADR-0125). Unsigned package builds in PR CI.
5. Local crash dumps with bounded retention, in the diagnostic report, and a "quit unexpectedly" notice (ADR-0123).
6. Licence compliance: notices directory, Qt source offer, LGPL exception in `LICENSE` as a release gate (ADR-0119).
7. Verification on clean machines of every supported OS (manual matrix below).

**Non-goals**
- Flatpak (ADR-0118, later plan), .rpm, arm64 (ADR-0124), APT repository, Homebrew cask, winget manifest (possible
  follow-ups after v2.0).
- In-app updater (ADR-0116); the update check itself is 0016.
- Crash upload or telemetry (ADR-0123).
- Native OS notifications for MCP consent: reconsidered and rejected — ADR-0108's own window stays.
- Bundling or installing SEGGER J-Link Software, its USB driver, or nRF Util.
- Migrating settings from the old app (different ids and formats; v1 and v2 install side by side until M8).

## User experience

**Downloads** (GitHub release page): `hio-monitor-<v>-x86_64.AppImage`, `hardwario-monitor_<v>_amd64.deb`,
`hio-monitor-<v>-macos-universal.dmg`, `hio-monitor-<v>-windows-x64.msi`, `SHA256SUMS`, symbol archives. Release
notes list the minimum OS versions and "Install SEGGER J-Link Software 8.00 or newer separately".

**Linux .deb:** `sudo apt install ./hardwario-monitor_2.0.0_amd64.deb` → app in the desktop menu ("HARDWARIO
Monitor", category Development/Electronics), `hio` in the terminal, J-Link USB access works for the logged-in user
without further steps (udev rule, ADR-0121). Uninstall removes the rule.

**AppImage:** `chmod +x`, run. A desktop entry is not installed (users may use AppImageLauncher / Gear Lever). CLI:
`./hio-monitor-2.0.0-x86_64.AppImage hio jlink list`, or a symlink named `hio` (ADR-0122). Without the udev rule the
doctor/setup wizard shows the ADR-0072 steps (unchanged).

**macOS:** open DMG → drag "HARDWARIO Monitor" to Applications (DMG window with an Applications link and a
background image). First start shows no Gatekeeper warning (notarized). Bluetooth permission prompt as in ADR-0086.

**Windows:** MSI wizard: licence (`LICENSE`), destination (default `C:\Program Files\HARDWARIO\Monitor`), features
("Add hio to PATH" on, "Desktop shortcut" off), install (UAC). Start menu "HARDWARIO Monitor". Silent install:
`msiexec /i hio-monitor-2.0.0-windows-x64.msi /qn [ADDDESKTOPSHORTCUT=1]`. Upgrades replace the older v2; a downgrade
shows "A newer version of HARDWARIO Monitor is already installed."

**After a crash:** the next GUI start shows a non-modal banner (translated): "HARDWARIO Monitor quit unexpectedly
last time. [Create diagnostic report] [Dismiss]". The report includes the crash files (ADR-0123).

## CLI

No new commands. Changed behaviour:
- `hio diag report` includes crash files (ADR-0123): `crashes/` folder in the zip, newest 3 files.
- `hio --version --json` gains `"package": "appimage" | "deb" | "macos-bundle" | "msi" | "dev"` (from a
  generated `hio/core/package_kind.h` set by the packaging preset; `dev` for normal builds). Also shown in the
  diagnostic report summary.

## Design

### Repository layout (`plan/0001-architecture.md` § `packaging/`)

```
packaging/
  README.md                     how packages are built locally and in CI, secrets, rotation, troubleshooting
  RELEASE.md                    release checklist (gates incl. ADR-0119 legal sign-off, tagging, publishing)
  common/
    icons/                      v1 icon reused: hio-monitor.icns + hio-monitor.ico copied from `main`
                                (resources/icons/icons.{icns,ico}), PNGs 16…512 extracted from the .icns (all LFS)
    licenses/                   third-party notice texts not already in assets (Qt LGPLv3+GPLv3, CLI11, replxx, miniz)
    licenses.cmake              installs LICENSE + notices + generated THIRD-PARTY-NOTICES.txt
  linux/
    Containerfile               Ubuntu 22.04 build image (ADR-0121), pinned by digest in CI
    com.hardwario.monitor.desktop
    com.hardwario.monitor.metainfo.xml
    60-hardwario-jlink.rules    ADR-0072 rule
    deb/postinst postrm         udevadm reload/trigger
    appimage/AppRun             argv[0]/first-arg dispatch (GUI / hio), libstdc++ checkrt
    build-appimage.sh           AppDir from the install tree + linuxdeploy (non-Qt deps) + appimagetool
  macos/
    entitlements.plist          ADR-0126
    dmg/background.png (LFS), dmg-settings  (create-dmg or hdiutil script)
    sign-and-notarize.sh
  windows/
    wix/                        WiX fragments: PATH feature, shortcuts with AppUserModelID, downgrade message
    sign.ps1                    wraps the Trusted Signing action/CLI for local dry-runs
cmake/HioPackaging.cmake        install(), deploy scripts, CPack config, package_kind
```

### Install and deploy (all OSes)

- `install(TARGETS hio-monitor hio …)` with a per-OS layout:
  - **Linux** (prefix `/opt/hardwario-monitor` for .deb, `AppDir/usr` for AppImage): `bin/hio-monitor`, `bin/hio`,
    `lib/` (Qt), `plugins/`, `qml/`, `translations/`, `licenses/`, `qt.conf`; RPATH `$ORIGIN/../lib`.
  - **macOS:** `HARDWARIO Monitor.app/Contents/{MacOS/hio-monitor, MacOS/hio, Frameworks/, PlugIns/, Resources/
    {qml, translations, licenses, hio-monitor.icns}}`; `hio` RPATH `@executable_path/../Frameworks`.
  - **Windows:** flat `bin`-style directory: `hio-monitor.exe`, `hio.exe`, Qt DLLs, `plugins` folders as
    windeployqt lays them out, `qml/`, `translations/`, `licenses/`, MSVC runtime DLLs app-local.
- Deploy: `qt_generate_deploy_qml_app_script(TARGET hio-monitor …)` (Qt ≥ 6.5 supports Linux, macOS, Windows) for the
  GUI, plus a CLI deploy that adds nothing beyond QtCore/QtNetwork/QtBluetooth already deployed. Qt translations
  `qtbase_cs.qm`, `qtdeclarative_cs.qm` (and `qtconnectivity_cs.qm` if present) are deployed (ADR-0027) — the app
  loads them from `QLibraryInfo::TranslationsPath`, which `qt.conf` points at `translations/`.
- QML import scan: `QT_QML_MODULE_NO_IMPORT_SCAN ON` stays for builds; the deploy script lists the needed QML modules
  explicitly (`QtQuick`, `QtQuick.Controls.Material`, `QtQuick.Layouts`, `QtQuick.Window`, …) — a CTest runs the
  installed GUI with `QT_QPA_PLATFORM=offscreen --self-test` (new hidden option: loads the main QML and exits 0)
  to prove the deployed tree is complete.
- Excluded from deploy: unused Qt plugins (`sqldrivers`, `multimedia`, `virtualkeyboard`, `designer`, …), QtGui-free
  CLI keeps its QtGui-free dependency set (ADR-0004).
- `package_kind`: cache variable `HIO_PACKAGE_KIND` (default `dev`), compiled into `hio/core/package_kind.h`.
- New presets: `linux-package` (inherits `linux-release`, `HIO_BUILD_TESTS=OFF` for the package build only — tests
  run in the gate job), `macos-package`, `windows-package`.

### Identity and desktop integration (ADR-0018)

- `QGuiApplication::setDesktopFileName("com.hardwario.monitor")` (Wayland icon / taskbar grouping) and
  `setWindowIcon` from the embedded icon; Windows: `SetCurrentProcessExplicitAppUserModelID(L"com.hardwario.monitor")`
  before the first window (in `app/gui/src/platform_win.cpp`).
- Icons: the v1 icon is reused (decision table): `icons.icns` (master, largest representation) and `icons.ico` from
  branch `main`; Linux hicolor PNGs 16…512 px are extracted from the .icns once by a documented script
  (`packaging/common/icons/extract.sh`); all binaries via LFS. A later brand icon replaces these files only. Windows `.rc` with the icon and VERSIONINFO (CompanyName "HARDWARIO a.s.",
  ProductName "HARDWARIO Monitor", FileVersion from `HIO_VERSION_NUMERIC`) for both executables.
- `.desktop`: `Name=HARDWARIO Monitor`, `Exec=hio-monitor`, `Icon=com.hardwario.monitor`,
  `Categories=Development;Electronics;`, `StartupWMClass=hio-monitor`, `Keywords=CHESTER;J-Link;RTT;console;`.
  AppStream metainfo with summary, description, screenshots (URLs to the repo), releases list generated from the
  version. Both validated in CI (`desktop-file-validate`, `appstreamcli validate --no-net`).

### Linux (ADR-0118, ADR-0121)

- Container `packaging/linux/Containerfile` (Ubuntu 22.04, pinned digest): LLVM 21 (apt.llvm.org jammy),
  `g++-13`/`libstdc++-13-dev` (toolchain PPA), Ninja, CMake ≥ 3.28 (Kitware APT), aqtinstall (pinned) Qt 6.8.x
  `linux_gcc_64` + qtconnectivity, Git LFS, `dpkg-dev`, `desktop-file-utils`, `appstream`. Built in CI and cached by
  digest; the image is not published.
  > **ADR-0177 (2026-10-26):** the ad-hoc 2.0.0-alpha.1 build showed that libstdc++ 13/14 did not compile the code:
  > `std::expected<hio::mcp::ToolResult, hio::Error>` is a Qt meta type, and libstdc++ before 15 has an
  > unconstrained `operator==` for `std::expected` that needs `ToolResult ==`. Resolved in code: every type inside a
  > meta-registered `std::expected` gets `operator==`, so `g++-13` stays and the .deb keeps `libstdc++6 (>= 13)`.
  > The container build is the regression check. Other results of that build: Qt 6.8.3 (aqtinstall 3.3.0) builds the
  > code cleanly; the AppImage needs GLIBC_2.35 at most; `libdbus-1` must stay bundled (excluding it breaks `hio` on
  > a clean 22.04); the container needs a UTF-8 locale for the tests. Ad-hoc scripts (reference only):
  > `/home/hardwario/src/monitor/appimage-adhoc/`.
- **.deb** (CPack DEB): package `hardwario-monitor`, maintainer "HARDWARIO a.s. <support e-mail, pending>", section
  `electronics`, `CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON` with the private Qt dir excluded from lookups, explicit
  `bluez` and `libstdc++6 (>= 13)`; files under `/opt/hardwario-monitor`, symlinks `/usr/bin/hio-monitor`,
  `/usr/bin/hio`, desktop file, metainfo, icons (hicolor) under `/usr/share`, udev rule under `/usr/lib/udev/rules.d/`.
  `postinst`/`postrm`: `udevadm control --reload-rules && udevadm trigger --subsystem-match=usb --attr-match=idVendor=1366`
  (errors ignored inside containers). `lintian` runs in CI; overrides only for `/opt` usage and embedded Qt.
- **AppImage:** AppDir from the install tree (`AppDir/usr` layout), `linuxdeploy` (pinned release, SHA-256 checked)
  **without** the Qt plugin to bundle remaining non-excluded libraries (e.g. `libxcb-cursor0`, `libxkbcommon-x11`),
  libstdc++/libgcc_s into `usr/optional/` with the checkrt logic in `AppRun`, then `appimagetool` (pinned, static
  type-2 runtime, no FUSE2 dependency where the runtime supports it). No zsync update info (ADR-0116).
- `AppRun` dispatch: `basename "$ARGV0"` = `hio` or first argument = `hio` → exec `usr/bin/hio` with the rest;
  otherwise exec `usr/bin/hio-monitor`.
- ADR-0058 single instance and ADR-0030 copies under AppImage: `AppLocalDataLocation` is the user's normal data dir,
  not the read-only mount, so both work; verified manually.

### macOS (ADR-0120, ADR-0126, ADR-0086)

- Bundle name `HARDWARIO Monitor.app` (`OUTPUT_NAME`-independent: the bundle directory is renamed at install via
  `MACOSX_BUNDLE_BUNDLE_NAME` + install rule; `CFBundleExecutable` stays `hio-monitor`). `Info.plist.in` from
  ADR-0086 gains `CFBundleIconFile`, `LSMinimumSystemVersion` 13.0, `NSHumanReadableCopyright`,
  `LSApplicationCategoryType public.app-category.developer-tools`.
- `hio` installed into `Contents/MacOS/` and fixed up by the deploy script (macdeployqt `-executable=`).
- Signing order (inside-out): Qt frameworks and plugins are re-signed with our identity (macdeployqt
  `-codesign`/`-hardened-runtime` or explicit `codesign` loop), then `hio`, then the bundle, each with
  `--options runtime --timestamp --entitlements packaging/macos/entitlements.plist`. `codesign --verify --deep
  --strict` and `spctl -a -t exec` in CI.
- DMG via `hdiutil` + a small AppleScript-free layout script (or `create-dmg` pinned), then sign the DMG, submit with
  `xcrun notarytool submit --key <p8> --key-id --issuer --wait`, `xcrun stapler staple`, `spctl -a -t open
  --context context:primary-signature`.
- J-Link copies (ADR-0030) keep SEGGER's signature; loading them is allowed by ADR-0126. Verified manually on a
  notarized build (load from `/Applications/SEGGER/JLink*`).

### Windows (ADR-0117, ADR-0120)

- windeployqt through the Qt deploy script, with `--no-compiler-runtime`; the MSVC runtime DLLs (`vcruntime140*.dll`,
  `msvcp140*.dll`) are copied app-local from the VS Build Tools redist directory (clang-cl links the MSVC STL).
- CPack **WIX** generator with WiX ≥ 4 (`CPACK_WIX_VERSION 4`, requires CMake ≥ 3.30 for the Windows packaging
  preset only), fixed `CPACK_WIX_UPGRADE_GUID`, per-machine (`CPACK_WIX_INSTALL_SCOPE perMachine`), product icon,
  `LICENSE` as RTF, patch/fragments in `packaging/windows/wix/` for: PATH (`Environment` element, feature
  `AddToPath`), Start-menu shortcut with `System.AppUserModel.ID`, optional desktop shortcut, `MajorUpgrade` with
  `DowngradeErrorMessage`.
- Signing: `hio-monitor.exe` and `hio.exe` after build, before CPack (CPack pre-build hook); MSI after CPack. Trusted
  Signing action pinned by SHA, OIDC login (`azure/login`), file digest SHA-256, RFC 3161 timestamp. `signtool
  verify /pa` in CI.
- ADR-0058 named pipe and ADR-0030 DLL copies with Microsoft Defender active: manual check (antivirus can lock a copy
  during deletion; ADR-0030 already ignores deletion failures and cleans up after 24 h).

### Crash handler (ADR-0123)

```cpp
// libs/core/include/hio/core/crash_handler.h
namespace hio::crash {
struct Config {
    std::filesystem::path directory;   // <AppLocalDataLocation>/crashes
    std::string_view appName;          // "hio-monitor" | "hio"
    std::string_view version;          // HIO_VERSION + git hash
};
[[nodiscard]] Result<void> install(const Config& config);    // once, first thing in main()
void pruneOld(const std::filesystem::path& directory);         // keep newest 5
[[nodiscard]] std::vector<std::filesystem::path> recent(const std::filesystem::path& directory, std::size_t max);
[[nodiscard]] bool crashedLastRun(const std::filesystem::path& directory);   // marker file written by the handler
void acknowledge(const std::filesystem::path& directory);                    // removes the marker
}
```
- Platform code in `libs/core/src/crash_handler_posix.cpp` / `crash_handler_win.cpp`. The Win32 dump call needs
  `dbghelp.lib` (system DLL, linked normally — not a vendor SDK). Allowed exceptions to the code rules are confined to
  these two files and listed in a header comment: C function pointers for signal handlers, `std::array` buffers,
  no allocation in the handler, `reinterpret_cast` for `EXCEPTION_POINTERS` only if unavoidable (needs a tidy NOLINT
  with reason).
- Disabled when built with sanitizers (`HIO_SANITIZER` defined) and when `HIO_NO_CRASH_HANDLER=1` is set.
- Diagnostic report (`libs/diag/src/report.cpp`): adds `crashes/` (newest 3 + macOS DiagnosticReports `hio*`
  newest 3), redaction applied to `.txt`; the 16 MiB zip bound still holds (dumps over the remaining budget are
  skipped and listed as skipped in `summary.txt`).
- GUI: `AppShellViewModel` exposes `crashedLastRun`; `CrashNoticeBanner.qml` in the shell. New `hio::Error` is not
  needed (it is a notice, not an error). Strings translated EN/CS.

### CI (ADR-0125)

- `ci.yml`: new job `package-<os>` per OS (after the release-preset build passes): builds the unsigned package, runs
  the deployed-tree self-test, `lintian` / `desktop-file-validate` / `appstreamcli` / `codesign --verify` (ad-hoc
  signature) / MSI `wix msi validate` (ICE), uploads the package (7-day retention).
- `release.yml` on `v2.*.*` tags, environment `release` (required reviewers, secrets only there):
  1. `gate` — version from tag equals tag; `check_translations` unfinished = 0; full test matrix (reuse `ci.yml` via
     `workflow_call`).
  2. `linux` (container), `macos`, `windows` — build, sign (mac/win), package, produce symbol archives
     (`.debug` split DWARF via `llvm-objcopy --only-keep-debug`, `.dSYM` via `dsymutil`, `.pdb`).
  3. `publish` — `SHA256SUMS`, `actions/attest-build-provenance` per artifact, `gh release create --draft
     [--prerelease] --generate-notes` with all assets.
- All actions pinned by commit SHA (as in `ci.yml`); `checkout` with `fetch-depth: 0` and `lfs: true`.
- Secrets (documented in `packaging/README.md`): `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD`,
  `MACOS_NOTARY_KEY_P8`, `MACOS_NOTARY_KEY_ID`, `MACOS_NOTARY_ISSUER`, `AZURE_CLIENT_ID`, `AZURE_TENANT_ID`,
  `AZURE_SUBSCRIPTION_ID`, `TRUSTED_SIGNING_ACCOUNT`, `TRUSTED_SIGNING_PROFILE`, `TRUSTED_SIGNING_ENDPOINT`.

### Licences (ADR-0119)

- `licenses/` in every package: `LICENSE` (HARDWARIO, with the LGPL exception once approved), `Qt-LGPL-3.0.txt`,
  `GPL-3.0.txt`, `Qt-SOURCE.txt` (Qt version, download URL of the exact source archive, written offer), fonts
  `OFL-1.1` (ADR-0048), Material Symbols `Apache-2.0`, CLI11 `BSD-3-Clause`, replxx (its licence files), miniz `MIT`,
  and a generated `THIRD-PARTY-NOTICES.txt` index (name, version, licence, URL) produced at configure time from a
  table in `packaging/common/licenses.cmake` (CTest checks every CPM dependency has a row).
- Settings › About (0006, extended in 0016) lists and opens these files from the installed `licenses/` dir.

### Error cases

No new `hio::Error` codes at runtime except in 0016. Package-time failures are CI failures with clear messages
(e.g. "notarytool: Invalid — see log <url>" printed in the job summary).

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Next milestone to plan | M6 / M7 / M8 | M7 (M6 plans already existed) | — |
| Where to write | new `v2/m7-plan` worktree / in m6-plan | `v2/m7-plan` in `../hio-monitor-m7-plan` from c2ffb86 | — |
| M7 split | two / per platform / one | two: packaging + release (0015), update check (0016) | ADR-0115 |
| Update mechanism | notify only / in-app updater / none | notify only | ADR-0116 |
| Windows installer | MSI WiX / Inno / Qt IFW / MSIX | MSI via WiX | ADR-0117 |
| MSI scope | per-machine / per-user / dual | per-machine | ADR-0117 |
| Linux packages | AppImage + .deb / + Flatpak / + .rpm | AppImage + .deb; Flatpak deferred | ADR-0118 |
| Licence conflict | LGPL exception / relicense / legal only | HARDWARIO licence + LGPL §4 exception, legal sign-off gates release | ADR-0119 |
| Signing | Apple + Azure Trusted Signing / Apple + HSM cert / macOS only | Apple Developer ID + Azure Trusted Signing | ADR-0120 |
| Linux baseline | 22.04 container / 24.04 only / private libstdc++ everywhere | 22.04 container; AppImage 22.04+, .deb 24.04+ | ADR-0121 |
| .deb Qt | private Qt in /opt / distro Qt | private Qt under `/opt/hardwario-monitor` | ADR-0121 |
| CLI | with GUI on PATH / separate / both | with GUI, on PATH | ADR-0122 |
| Crash reporting | local minidumps / none / opt-in upload | local only | ADR-0123 |
| Crash mechanism | own handler / sentry-native / OS only | own minimal handler | ADR-0123 |
| Architectures | x64 + mac universal / + Linux arm64 / + Win arm64 | x64 + macOS universal | ADR-0124 |
| Release trigger | tag → draft / dispatch / release created | tag → draft release | ADR-0125 |
| macOS library validation | disable-library-validation / re-sign | disable-library-validation | ADR-0126 |
| App icon source | brand team SVG / reuse v1 icon / drafted SVG | reuse the v1 icon (`icons.icns`, `icons.ico` from `main`) | — |
| MCP consent as native notification (ADR-0108 asked M7) | keep own window / + info notification / native with buttons | keep the own window (ADR-0108 unchanged) | ADR-0108 |
| libstdc++ 13/14 vs `std::expected` meta types (2026-10-26) | `operator==` on the types / require libstdc++ 15 | `operator==` on the types; g++-13 stays | ADR-0177 |
| Minor defaults: install paths and names above; .deb package `hardwario-monitor`, section `electronics`, maintainer e-mail; udev file `60-hardwario-jlink.rules`; macOS bundle `HARDWARIO Monitor.app`; desktop categories; AppImage tooling (CMake deploy + linuxdeploy without Qt plugin + appimagetool, no zsync); MSVC runtime app-local; WiX ≥ 4 via CPack (CMake ≥ 3.30 for Windows packaging only); desktop shortcut off; crash retention 5 files / report includes 3; `--self-test` hidden GUI option; `"package"` field in `--version --json`; unsigned PR packages, 7-day retention; artifact names above | accept / change | accepted (2026-10-19); the .deb maintainer e-mail is supplied by HARDWARIO | — |

## Implementation steps

**Phase 0 — ADR-0177**
- [x] 01 `operator==` on `hio::mcp::ToolResult` and on every other type inside a meta-registered `std::expected`;
  checked by a libstdc++ 13 build (phase C1 container).

**Phase A — common install tree**
- [ ] A1 `cmake/HioPackaging.cmake`: install rules per OS, `qt_generate_deploy_qml_app_script`, Qt `cs`
  translations, `qt.conf`, `HIO_PACKAGE_KIND` + `package_kind.h`, `*-package` presets.
- [ ] A2 `--self-test` GUI option and the installed-tree CTest (offscreen) on all three OSes.
- [ ] A3 Icons (v1 icon reused), `.rc` + VERSIONINFO, `setDesktopFileName`, AppUserModelID, Info.plist keys.
- [ ] A4 Licences: `packaging/common/licenses/`, `licenses.cmake`, `THIRD-PARTY-NOTICES.txt`, CPM coverage CTest.

**Phase B — crash handler**
- [ ] B1 `hio::crash` POSIX + Windows, unit tests with a crashing child process (each signal / exception), pruning.
- [ ] B2 Diagnostic report `crashes/` (ADR-0075 bounds), CLI `hio diag report` test.
- [ ] B3 GUI `CrashNoticeBanner` + view-model property, Qt Quick test, translations.

**Phase C — Linux**
- [ ] C1 Containerfile + CI build in the container; check `objdump -T` max `GLIBC_` ≤ 2.35.
- [ ] C2 .deb (CPack DEB, symlinks, udev rule, postinst/postrm, desktop/metainfo/icons), lintian clean.
- [ ] C3 AppImage (AppDir, linuxdeploy, AppRun dispatch + checkrt, appimagetool).

**Phase D — macOS**
- [ ] D1 Bundle layout, `hio` inside, entitlements, inside-out signing script, verify (ad-hoc in PR CI).
- [ ] D2 DMG layout, notarization + stapling (release only), entitlement CTest.

**Phase E — Windows**
- [ ] E1 windeployqt + app-local MSVC runtime, WiX via CPack, fragments (PATH, shortcuts, downgrade), ICE validation.
- [ ] E2 Trusted Signing integration (exe/dll before CPack, MSI after), `signtool verify`.

**Phase F — CI and release**
- [ ] F1 `package-<os>` jobs in `ci.yml`.
- [ ] F2 `release.yml` (gate, builds, symbols, checksums, attestations, draft release), `release` environment,
  tag protection rule (repository setting — done by a maintainer, listed in RELEASE.md).
- [ ] F3 `packaging/README.md`, `packaging/RELEASE.md`; dry-run with a `v2.0.0-rc.0` tag on a fork or with signing
  steps in "dry-run" mode.
- [ ] F4 Manual verification matrix (below) on the RC; record results in this plan.

## Tests and verification

Automated:
- Installed-tree self-test (GUI offscreen loads QML; `hio --version --json` runs from the installed tree) on all OSes.
- Unit tests: crash handler (child process crashes → file written, re-raise, pruning), report inclusion and bounds.
- Packaging validators: `lintian`, `desktop-file-validate`, `appstreamcli validate`, glibc symbol ceiling, macOS
  `codesign --verify --strict`, entitlements check, `spctl` (release), WiX ICE validation, `signtool verify`
  (release), licence coverage CTest.
- Normal gates stay: ASan/UBSan/TSan, clang-tidy zero findings, clang-format, qmllint, `check_translations`.

Manual (clean VMs / machines; J-Link + CHESTER for the hardware rows):

| OS | Package | Checks |
|---|---|---|
| Ubuntu 22.04 (X11) | AppImage | starts, `hio` dispatch, Qt `cs` texts in Czech UI, doctor shows udev steps, RTT console |
| Ubuntu 24.04 (Wayland) | .deb | install/uninstall, menu entry + icon, `hio` on PATH, udev rule works without replug-as-root, BLE scan, RTT, flash |
| Debian 13 | .deb | install, start |
| Fedora 40+ | AppImage | start, RTT console |
| macOS 13 Intel, macOS 15 Apple silicon | DMG | no Gatekeeper warning, J-Link loads (ADR-0126), Bluetooth prompt (GUI and `hio` in Terminal), `hio` path in About |
| Windows 10 22H2, Windows 11 | MSI | install/upgrade/downgrade-block/uninstall, SmartScreen, `hio` in new terminal, single instance (ADR-0058), J-Link copies with Defender on, silent install |
| all | — | crash notice after a forced crash (`HIO_TEST_CRASH` only in debug builds), report contains crash files |

## Risks and open points

- **Azure Trusted Signing eligibility** for HARDWARIO a.s. (identity validation can take weeks) — check first; fall back
  to a cloud-HSM OV certificate via a new ADR.
- **Legal sign-off on the LGPL exception** (ADR-0119) may delay the first release; engineering work is not blocked.
- Qt 6.8 `linux_gcc_64` needs `libxcb-cursor0` (not installed by default on some distros) → bundled in the AppImage,
  listed as a .deb dependency.
- AppImage needs FUSE on the host unless the static runtime's fallback is used; document `--appimage-extract-and-run`.
- macOS: re-signing Qt frameworks inside-out must not break the Bluetooth plist embedded in `hio` (ADR-0086) — checked
  by the existing CTest after signing.
- WiX ≥ 4 support in CPack needs CMake ≥ 3.30 on the Windows runner; if it is not adequate, use `wix build` directly
  on own `.wxs` (same decisions).
- The 22.04 container must keep LLVM 21 and libstdc++ 13 packages available; pin package versions and cache the image.
  libstdc++ 13 also guards ADR-0177 (meta-registered `std::expected` types need `operator==`).
- Old-app coexistence: v1 installs as "HARDWARIO Monitor" too (Windows Start menu / Program Files folder
  `HARDWARIO Monitor`; macOS bundle built from `hiomon`). v2 uses `Program Files\HARDWARIO\Monitor` and
  `HARDWARIO Monitor.app`; check on a machine with v1 installed that the two entries are distinguishable (v2 adds no
  suffix; release notes explain the side-by-side period until M8).
