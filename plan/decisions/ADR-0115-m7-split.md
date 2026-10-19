# ADR-0115 — M7 is split into a packaging + release plan and an update-check plan

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M7 planning session)

## Context

M7 covers packages for three operating systems, signing, the release workflow, licence notices, crash dumps and the
update check (roadmap). Most of it is build/CI work with no runtime code; the update check is a normal backend + GUI +
CLI feature.

## Options considered

1. **Two plans: packaging + release pipeline, and update check + release UX (chosen).**
2. One plan per platform plus a release/update-check plan (four plans).
3. One plan.

## Decision

- [0015](../features/0015-m7-packaging-release.md): install/deploy rules, icons and desktop metadata, AppImage,
  .deb, DMG, MSI, signing, the release workflow, licence notices, crash dumps.
- [0016](../features/0016-m7-update-check.md): `UpdateChecker`, the GUI banner and About entries, `hio update check`.
- 0016 can be implemented in parallel with 0015. Its manual end-to-end check (an installed old build sees a newer
  draft-free release) needs 0015's packages.

## Consequences

- Platform work stays in one place, so shared parts (install layout, notices, release gates) are not duplicated.
- 0015 is large; its implementation steps are phased per platform so that each phase can be verified on its own.
