# ADR-0053 — Non-blocking `DeviceSession::open`

- **Status:** Accepted (amends feature 0002 and ADR-0031)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 GUI planning session)

## Context

Feature 0002 specifies that `DeviceSession::open()` returns once the probe is open and the target connected.
With 3 connect attempts 500 ms apart plus library loading this blocks for seconds; the GUI thread must never
block on hardware (ADR-0031).

## Options considered

1. **Make `open()` non-blocking; connection progress reported only through events (chosen).**
2. Keep the API; the GUI calls `open()` on a short-lived thread and moves the façade to the main thread.

## Decision

- `open()` validates options, reserves the `SessionRegistry` slot (errors: `internal.session_limit`,
  `probe.open_failed` "already used") and starts the worker; it returns immediately.
- Library copy load, probe open and target connect run on the worker and are reported as
  `Connecting` → `Connected`, or `Disconnected` / `Stopped` with the `hio::Error` that 0002 already defines.
- The CLI keeps its blocking behaviour with a helper
  `[[nodiscard]] Status waitUntilConnected(DeviceSession&, std::chrono::milliseconds timeout)` that runs a
  local event loop until the first `Connected` / failure event.
- Feature 0002 is amended (Design, steps D*, tests) before implementation of M1 starts.

## Consequences

- One code path for GUI and CLI; GUI shows "Connecting…" in the device bar with a Cancel (= `close()`).
- Tests must cover close() during connect (stop_token honoured by every connect step).
