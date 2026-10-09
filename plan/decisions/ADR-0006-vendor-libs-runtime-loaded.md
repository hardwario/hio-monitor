# ADR-0006 — Vendor libraries loaded at runtime from the user's installation

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (bootstrap planning session)

## Context

The old app vendored ~380 MB of J-Link/nrfjprog binaries in git and linked them at build time. Redistribution rights and version staleness are a concern.

## Decision

Load the JLinkARM library at runtime (`dlopen` / `LoadLibrary`) from the user's installed SEGGER J-Link Software (and locate the optional nRF Util executable, ADR-0016), with standard-path discovery and a manual path override in Settings. Do not ship or commit them. The setup wizard guides installation.

## Consequences

- No vendor binaries in the repo or installers.
- Missing/incompatible library is a normal, well-explained error with a wizard.
- Function tables are typed and version-checked in one adapter module.

_Note (2026-10-09): nrfjprog is no longer used (ADR-0015)._
