# ADR-0131 — The BLE thread is an owned `std::jthread` running a `QEventLoop`

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad

Supersedes the `QThread` wording of ADR-0080 and ADR-0130; their other decisions stay.

## Context

ADR-0080 put the BLE host on an owned `QThread`. In the TSan build `QThread::wait()` is not visible to
ThreadSanitizer, so destroying a host reported data races on its shared state, and creating and ending several
hosts quickly aborted TSan (`sanitizer_thread_registry.cpp:186 live_.try_emplace`); about 77 TSan tests failed.
CLAUDE.md allows both `std::jthread` and owned `QThread`.

## Options considered

1. **Owned `std::jthread` that runs a `QEventLoop` (Qt adopts the thread) (chosen).**
2. Keep `QThread` and find another way to make the TSan job pass (suppressions or excluding tests).

## Decision

`BleHost` owns a `std::jthread`; the thread creates the `BleWorker`, runs a `QEventLoop` (so `QTimer` and QtBluetooth
work there), and destroys every QtBluetooth object before the loop ends. `stop()` follows ADR-0130 (warning after
3 s, then keeps waiting) and joins the thread.

## Consequences

- TSan sees the join; the BLE tests run clean in the TSan job.
- Qt treats the thread as an adopted thread; nothing in the design relies on `QThread`-only APIs.
