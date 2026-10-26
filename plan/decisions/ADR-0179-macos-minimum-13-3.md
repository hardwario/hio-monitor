# ADR-0179 — macOS minimum version 13.3

- **Status:** Accepted (amends ADR-0012)
- **Date:** 2026-10-26
- **Decided by:** Matej Strnad

## Context

ADR-0012 set the macOS minimum to 13 (Ventura); every `macos-*` preset sets
`CMAKE_OSX_DEPLOYMENT_TARGET=13.0`. The code uses `std::format` (CLAUDE.md: no `printf` family).
Apple's libc++ enables `std::format` (`__cpp_lib_format`) only when the deployment target is
**13.3 or newer**, because formatting floating-point values needs `std::to_chars` for `double`,
which Apple ships in the system libc++ dylib only from macOS 13.3. With 13.0 every `macos-*` CI job
fails at configure / compile time.

## Options considered

1. **Raise the minimum to macOS 13.3.** One-line preset change; 13.3 is a free point update of
   Ventura (March 2023), so no hardware loses support.
2. Keep 13.0 and replace `std::format` with Qt / `fmt` on macOS — a second formatting path on one
   platform, against ADR-0001 and CLAUDE.md.
3. Bundle a newer libc++ — not possible with the system C++ runtime on macOS.

## Decision

The macOS minimum is **13.3** (universal). All `macos-*` presets set
`CMAKE_OSX_DEPLOYMENT_TARGET=13.3`; packaging metadata (`LSMinimumSystemVersion`, plan 0015) uses
13.3 as well. The rest of ADR-0012 is unchanged.

## Consequences

- `std::format` and float `std::to_chars` work on macOS without workarounds.
- Users on macOS 13.0–13.2 must install the free 13.3+ update; release notes and the download page
  say "macOS 13.3 or newer".
- Plan 0015 (`v2/m7-packaging`) must use `LSMinimumSystemVersion` 13.3 and test on 13.3+.
