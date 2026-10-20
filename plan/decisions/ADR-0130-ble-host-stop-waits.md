# ADR-0130 — `BleHost::stop()` warns after 3 s, then keeps waiting for the thread

- **Status:** Accepted; thread type superseded by ADR-0141 (`std::jthread`)
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad

Amends ADR-0080 ("joins with a 3 s bound, then logs and continues").

## Context

Destroying a `QThread` that still runs aborts the process; leaving it running (leaking it) breaks the rule that
threads are owned objects with explicit stop and join (CLAUDE.md).

## Options considered

1. **Wait 3 s, log a warning, then keep waiting without a bound (chosen).**
2. Wait 3 s, then leak the thread.
3. Wait 3 s, then terminate the thread.

## Decision

`BleHost::stop()` disconnects all links, deletes the QtBluetooth objects on the BLE thread, quits the loop and waits.
After 3 s it logs a warning (`lcBle`) and keeps waiting until the thread has finished.

## Consequences

- No abort and no leaked thread; a hung platform stack can delay application exit, which is visible in the log.
