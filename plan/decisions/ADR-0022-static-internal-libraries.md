# ADR-0022 — Internal libraries are static

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M0 planning session)

## Context

The backend is split into several CMake libraries (`libs/*`) used by the GUI, the CLI and tests.

## Options considered

1. Static libraries.
2. Shared libraries (smaller install, but export macros, rpath and Windows DLL-boundary issues on three OSes).

## Decision

All `libs/*` targets are **static libraries**. Qt is still linked **dynamically** (LGPLv3, ADR-0002).

## Consequences

- No export macros or DLL boundaries in our code; simpler packaging.
- GUI and CLI each contain their own copy of the backend (acceptable size cost).
- Static initialization order across libs must not be relied upon.
