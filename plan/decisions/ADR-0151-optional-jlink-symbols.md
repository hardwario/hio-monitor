# ADR-0151 — New JLinkARM functions for target operations are resolved optionally

- **Status:** Accepted (list corrected 2026-10-20 by the user: `JLINKARM_ReadMemU16` dropped — 16-bit reads use `JLINKARM_ReadMemEx` with access width 2, as pylink 2.0.2)
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (M6 implementation session, sync of plan 0013 with the code)

## Context

M6 adds JLinkARM functions for target operations (`JLINKARM_IsHalted`, `JLINKARM_GetId`, `JLINKARM_WriteU16`,
`JLINKARM_WriteU32`, `JLINKARM_ReadMemU16`, `JLINKARM_GetRegisterList`, `JLINKARM_GetRegisterName`,
`JLINKARM_ReadRegs`, `JLINK_GetMemZones`). Until now every resolved symbol is mandatory: one missing export makes
the library load fail with `dependency.jlink_load_failed`, so RTT and flashing would stop working too.

## Options considered

1. **Optional** resolution for the new functions.
2. Mandatory, as today (they exist in J-Link 8.00, ADR-0029).

## Decision

Option 1. The functions listed above (and any later function used only by low-level target operations) are
resolved optionally. A missing one makes only the operation that needs it fail with the new error
`dependency.jlink_function_missing` (detail: the function name). The symbols used by M1/M2 stay mandatory.

## Consequences

- RTT and flashing keep working with an unusual or stripped J-Link library.
- `IJLinkApi` reports availability per function; the fake library can omit them to test the error path.
