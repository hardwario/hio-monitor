# ADR-0124 — Release architectures: x86_64 and macOS universal; arm64 deferred

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M7 planning session)

## Context

Open question 13. J-Link Software exists for Linux arm64 and Windows arm64; Qt 6.8 has binaries for both. Each new
architecture adds CI cost and a manual test matrix.

## Options considered

1. **x86_64 on Linux and Windows, universal (arm64 + x86_64) on macOS (chosen).**
2. Add Linux arm64.
3. Add Linux and Windows arm64.

## Decision

- v2.0 release packages: Linux x86_64 (AppImage, .deb), Windows x64 (MSI), macOS universal (DMG, ADR-0012).
- Linux arm64 (e.g. Raspberry Pi 5) and Windows arm64 are deferred; open question 13 stays open for after v2.0.
- Nothing in the code may assume x86_64 (already true: no inline assembly, no architecture-specific vendor
  declarations).

## Consequences

- Windows on ARM users run the x64 build under emulation; the J-Link DLL must then also be x64 (the setup wizard
  says so if the architecture does not match).
