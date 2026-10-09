# ADR-0042 — Minimum nRF Util version

- **Status:** Proposed (to be Accepted with the results of the M2 Phase 0 spike)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M2 planning session)

## Context

Modem firmware updates use the optional nRF Util (ADR-0016): `nrfutil` core plus the `device` command, with JSON
output. Open-questions #14 asked for the minimum version. The JSON format and `device program` options have changed
between releases.

## Options considered

1. The versions tested in the M2 spike become the minimum (chosen).
2. Require the latest release at planning time.

## Decision

The M2 Phase 0 spike tests the current nRF Util core and `device` command on nRF9160 and nRF9151. The tested
versions are recorded here as minimums: core **_tbd_**, `device` **_tbd_**. Older versions →
`dependency.nrfutil_too_old`; missing `device` command → `dependency.nrfutil_device_missing`.

## Consequences

- Only versions we actually tested are accepted; raising the minimum later needs a new ADR.
- The setup wizard (M4) shows the minimum and the upgrade commands.
