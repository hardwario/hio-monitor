# ADR-0137 — py-hardwario deprecation is outside M8

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M8 planning session)

## Context

`hio` aims to be a drop-in for py-hardwario's `hardwario` command (0006-cli.md). Whether py-hardwario is deprecated
affects another repository and team.

## Options considered

1. **Out of scope; M8 only verifies compatibility (chosen).**
2. M8 ships a py-hardwario release with a deprecation notice.

## Decision

M8 delivers the compatibility table and test (ADR-0139) and documents the migration in `docs/`. Any py-hardwario
deprecation is decided by its maintainers separately.

## Consequences

- Both tools coexist after v2.0.0; `hio` documentation names the intentional differences.
