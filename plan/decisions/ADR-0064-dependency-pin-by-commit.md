# ADR-0064 — Pinning a dependency to a commit when it has no recent tag

- **Status:** Accepted (amends ADR-0010)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M1 implementation start)

## Context

ADR-0010 pins every third-party dependency to an exact tag plus a hash. replxx (CLI line editor, ADR-0033) has
only one tagged release, `release-0.0.4` from 2021. Its master branch carries years of fixes since then, among
them fixes for Windows consoles and UTF-8 handling that the `hio` console needs.

## Options considered

1. The tag `release-0.0.4`: follows ADR-0010 literally, but needs local patches or workarounds.
2. A specific master commit as a source archive, with its SHA-256.
3. Build both, then decide.

## Decision

A dependency **may be pinned to a commit** (archive URL of that exact commit + `URL_HASH SHA256=…`) when:

- it has no tagged release within roughly the last two years, **and**
- fixes we need exist only after its latest tag.

The commit hash, the date, the SHA-256 and the reason are recorded in the feature plan that adds the dependency
and in a comment next to the `CPMAddPackage` call in `cmake/HioDeps.cmake`. Branch names (`master`, `main`) are
never used as pins. The decision applies first to replxx (plan 0002). Every other dependency stays tag-pinned
under ADR-0010.

## Consequences

- We get the needed fixes without carrying patches.
- An upgrade means picking a newer commit and reviewing its changes deliberately, since there are no release
  notes to rely on.
- If the upstream project tags a release that contains the fixes, we switch to that tag.
