# ADR-0002 — Qt 6.8 LTS, Community (LGPLv3), official binaries

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (bootstrap planning session)

## Context

Qt Community license is required. The old app used Qt 6.6. Long product lifetime favours LTS releases. Building Qt from source (Conan/vcpkg) was rejected as too heavy.

## Decision

Use **Qt 6.8 LTS** official binaries (Qt online installer locally, pinned `aqtinstall` in CI). Comply with LGPLv3: **dynamic linking only**, no commercial-only modules, ship Qt license notices, allow replacing Qt libraries. Move to the next LTS via a new ADR.

## Consequences

- Qt is not managed by CPM/FetchContent.
- Packaging must keep Qt as shared libraries and include license texts.
- Minimum OS versions follow Qt 6.8 (see ADR-0012).
