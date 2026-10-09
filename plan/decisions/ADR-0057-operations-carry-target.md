# ADR-0057 — Session operations carry their target MCU

- **Status:** Accepted (amends feature 0003 and ADR-0031)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 GUI review of mockups)

## Context

With "what to flash" first (ADR-0055), a tab whose console is attached to the application MCU can flash the LTE
MCU or the modem through the same probe. In M1/M2 a `DeviceSession` is bound to one target, and
`SessionRegistry` allows one session per probe serial number.

## Options considered

1. **Operations carry an explicit target; the session temporarily connects the probe to it (chosen).**
2. Retarget the whole tab (disconnect the console and switch to `lte`).
3. A temporary second session for the same probe (needs a registry exception).

## Decision

- `flash`, `erase`, `recover`, `reset`, `readMemory` and `updateModem` take a `std::string_view targetId` that
  names a target of the session's profile.
- On the worker, an operation for a target other than the console target runs these steps:
  1. Pause RTT (ADR-0034, "stop without reporting a disconnect").
  2. Connect the probe to the operation target's J-Link device. `updateModem` instead releases the probe for nRF Util
     (suspend).
  3. Run the operation.
  4. Reconnect to the console target and re-attach RTT.
- Step 4 also runs on failure and on cancel. If the reconnect fails, the session reports `Disconnected` with the usual
  errors and auto-reconnect applies.
- The one-session-per-probe rule stays, so no second library copy is needed for the same probe.
- `OperationProgress` gains the phases `SwitchingTarget` and `Restoring` (shown in the stepper only when they
  apply).
- The CLI is unchanged: `hio chester lte flash` opens its session on `lte`, so source and operation target are the
  same.

## Consequences

- Feature 0003 must be amended (API signatures, workflow tests: cross-target flash, reconnect after failure).
- The GUI Flash page works from any tab state. A tab with no session opens one for the chosen kind's target with
  `attachRtt = false`.
