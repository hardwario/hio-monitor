# 0005 — Build, CI and release

- **Status:** Draft (constraints decided — ADR-0001, 0002, 0010, 0011, 0012, 0021, 0022; details in [features/0001](features/0001-m0-skeleton.md) and the M7 plan)

## Toolchains

| OS | Compiler | Std lib | Qt binaries |
|---|---|---|---|
| Linux x86_64 | clang **21** (ADR-0021) | system libstdc++ (≥ 13 for C++23 features) | Qt 6.8 LTS `linux_gcc_64` |
| macOS universal | Apple clang (Xcode 16+) (ADR-0021) | libc++ | Qt 6.8 LTS `clang_64` |
| Windows x64 | clang-cl **21** + MSVC Build Tools (SDK, linker) | MSVC STL | Qt 6.8 LTS `win64_msvc2022_64` |

clang-format and clang-tidy: LLVM **21** everywhere. Linux CI host: GitHub `ubuntu-24.04` (ADR-0021).
Internal libraries are **static**; Qt is linked dynamically (ADR-0022).

Only C++23 features available in all three std libs may be used (tracked in `cmake/` feature checks).

### Allowed C++23 library features (checked by `cmake/HioCxxFeatureChecks.cmake`)

| Feature | Macro | Status |
|---|---|---|
| `std::expected` | `__cpp_lib_expected >= 202211L` | allowed |
| `std::format` | `__cpp_lib_format` | allowed |
| `std::jthread`, `std::stop_token` | `__cpp_lib_jthread` | allowed (verify Apple libc++) |
| `std::span` | `__cpp_lib_span` | allowed |
| ranges (C++20 core) | `__cpp_lib_ranges` | allowed |
| `std::print` | `__cpp_lib_print` | not yet — use `std::format` + streams |

New entries require a check on all three std libs before use.
Qt is obtained as official binaries: Qt online installer locally, `aqtinstall` (pinned version) in CI.

## CMake

- `CMakePresets.json` with presets per OS × {Debug, Release, ASan+UBSan, TSan, clang-tidy}.
- Third-party libs via CPM.cmake / FetchContent, each pinned to exact tag **and** hash; a CPM
  source cache for offline/CI reuse. Candidate deps (each requires user approval in a plan):
  GoogleTest/GMock, possibly a JSON library (or QJsonDocument), possibly an MCP SDK.
- Strict warnings as errors; hardened std-lib assertions in Debug.

## CI (GitHub Actions)

- On every PR and push to `v2-dev`: build + unit tests on Linux, Windows, macOS; ASan/UBSan and
  TSan jobs on Linux (and macOS where possible); clang-tidy and clang-format check; QML lint.
- Hardware-in-the-loop tests: opt-in self-hosted runner with a CHESTER + J-Link, `hil.yml` on dispatch and `v2.*` tags, part of the release gate (ADR-0140, [features/0017](features/0017-m8-parity.md)).

## Packaging

| OS | Artifacts |
|---|---|
| Linux | AppImage (Ubuntu 22.04+), .deb (Ubuntu 24.04+, private Qt in `/opt`, J-Link udev rule) — ADR-0118, ADR-0121; Flatpak deferred |
| macOS | universal signed + notarized DMG, hardened runtime (ADR-0126) |
| Windows | signed per-machine MSI (WiX), `hio` on PATH (ADR-0117) |

Details, signing (ADR-0120) and the tag-triggered release workflow (ADR-0125): [features/0015](features/0015-m7-packaging-release.md).

Vendor software (J-Link library, optional nRF Util) is **not** bundled (ADR-0006, ADR-0016); installers and the setup
wizard point users to the official downloads.

## Versioning

Semantic versioning; version injected by CMake from git tags (no sed patching). v2 starts at `2.0.0`.
