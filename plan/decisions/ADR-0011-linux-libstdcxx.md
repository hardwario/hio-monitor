# ADR-0011 — Linux uses the system libstdc++

- **Status:** Accepted (amended by ADR-0121: release builds use a Ubuntu 22.04 container with libstdc++ 13 from the toolchain PPA; the AppImage bundles libstdc++)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (bootstrap planning session)

## Context

Official Qt Linux binaries are built against libstdc++; mixing with libc++ is risky.

## Decision

On Linux, clang uses the **system libstdc++** (≥ 13 required for the C++23 features we use).

## Consequences

- Build hosts need GCC 13+ libstdc++ headers; packaging (AppImage/Flatpak) must provide a sufficiently new libstdc++ runtime on older distros like Ubuntu 22.04.
