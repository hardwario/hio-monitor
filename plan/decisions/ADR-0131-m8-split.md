# ADR-0131 — M8 is split into a parity plan and a beta + release + retirement plan

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M8 planning session)

## Context

M8 (roadmap) finishes v2.0: every old-app feature has an equivalent (0002 parity checklist), the CLI is a drop-in for
py-hardwario, staff test release candidates, v2.0.0 is released and the old app is retired.

## Options considered

1. **Two plans: parity (code) and beta + release + retirement (process) (chosen).**
2. Three plans (parity, beta, retirement).
3. One plan.

## Decision

- [0017](../features/0017-m8-parity.md): parity audit of 0002, J-Link firmware update in the GUI (ADR-0132), CLI
  compatibility table and test (ADR-0139), hardware-in-the-loop CI (ADR-0140), user documentation in `docs/`
  (ADR-0138).
- [0018](../features/0018-m8-beta-release-retirement.md): staff release candidates and exit criteria (ADR-0135), the
  v2.0.0 release and v1 user migration (ADR-0136), branch switch (ADR-0133, ADR-0134).
- 0018's exit criteria require 0017 to be Done.

## Consequences

- Engineering work and release process are reviewed separately; 0018 contains almost no code.
