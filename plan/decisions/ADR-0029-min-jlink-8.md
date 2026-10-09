# ADR-0029 — Minimum J-Link Software version 8.00

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M1 planning session)

## Context

open-questions #14: the minimum supported J-Link Software version was open. The old app bundled
7.94.11. Older versions lack device support for newer Nordic chips (nRF9151) and differ in behaviour;
supporting many versions multiplies testing.

## Options considered

1. 7.94 (widely installed, what the old app shipped).
2. 8.x (current; best Nordic support; forces some users to update).
3. No minimum, warning only.

## Decision

Require **J-Link Software 8.00 or newer**. At load time `JLINKARM_GetDLLVersion()` (format
`major*10000 + minor*100 + revision`) must be `>= 80000`; an older library is rejected with
`dependency.jlink_too_old` (message names the found and required version, suggestion to install the
current J-Link Software, wizard FirstRunSetup). The lowest version actually **tested** on hardware is
recorded in the M1 feature plan and logged at startup when a lower 8.x is found.

This resolves the J-Link part of open-questions #14; the minimum nRF Util version is decided with M2.

## Consequences

- Users with 7.x must update J-Link Software; the setup wizard (M4) and the error explain how.
- rttt / pylink behaviour was observed with 7.x; differences on 8.x are a risk tracked by M1 HIL tests.
- Raising the minimum later requires a new ADR.
