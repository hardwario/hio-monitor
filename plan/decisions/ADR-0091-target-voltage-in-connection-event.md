# ADR-0091 — Target voltage in the connection event

- **Status:** Accepted (amends feature 0002; added on `v2/m2-flashing` for feature 0004)
- **Date:** 2026-10-12
- **Decided by:** Matej Strnad

## Context

The M3 GUI (feature 0004) shows the target voltage next to the connection state of a tab, so the user
sees at once whether the board is powered. The M1 session worker already reads VTref through the probe
(after a connect and every second for the power-loss detection, ADR-0034), but the value never leaves the
worker: `ConnectionEvent` carries only the state and an optional error.

## Options considered

1. **`ConnectionEvent` carries the last measured voltage (chosen).** No new polling, no new event type.
2. A separate `targetVoltageChanged` signal with its own events (extra traffic, extra API).
3. A GUI-side query (`DeviceSession::targetVoltage()`) — needs a worker round trip or shared state.

## Decision

- `ConnectionEvent` gains `std::optional<int> targetMillivolts` (the repo's `HardwareStatus::targetMillivolts`
  naming and type): the VTref the session's last power reading measured when the event is emitted.
- It is set at least on `Connected`; it is empty when nothing was measured yet, the probe does not answer, or a
  probe that cannot measure VTref reads 0 mV before any healthy reading (ADR-0034 power rules).
- Only the readings the worker already makes are used (after a connect, the 1 s power check); no new polling
  and no extra connection events (events are still emitted only on a state or error change).
- The fake J-Link library's scenario `vtref` timeline sets the value in tests.
- The CLI is unchanged.

## Consequences

- The GUI shows the voltage from `connection()` / `connectionChanged` without any new API.
- The value can be up to one power-check interval old; voltage changes alone do not emit events.
- Designated initializers of `ConnectionEvent` name the new field.
