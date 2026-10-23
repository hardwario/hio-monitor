# ADR-0157 — RTT stays attached during light target operations

- **Status:** Accepted
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (M6 implementation, question from the 0013 implementation agent)

## Context

The new operations `halt`, `go`, `targetStatus`, `readMemory`, `writeMemory`, `readRegisters` and `memoryZones` run
through the M2 operation machinery, which pauses RTT and re-attaches it afterwards. After `halt` the console would
report Disconnected until `go`/`reconnect`/`resume`. rttt keeps RTT working while the core is halted and reads
memory live.

## Options considered

1. **Keep RTT attached** for these operations on the console target (like `Diagnose`'s `keepRtt`).
2. M2 pattern: pause, run, re-attach.

## Decision

Option 1. On the session's console target the light operations run with RTT attached (no suspend, no re-attach);
a halted core keeps the console Connected. On another target (ADR-0057) they follow the M2 pattern. Programming
operations (`flash`, `erase`, `recover`, `writeFlash`, `reset`) are unchanged.

## Consequences

- rttt parity; no re-attach on every memory read. Tests cover the console state after `halt`/`go`.
