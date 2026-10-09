# ADR-0016 — nRF91 modem firmware updates via optional nRF Util

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad

## Context

After ADR-0015 everything is programmed through the J-Link library, except nRF91 **modem
firmware** (`modem.zip`, e.g. inside a CHESTER LTE bundle with `modem.zip` + `application.hex`).
Modem firmware is not written by ordinary flash writes; Nordic tools use a dedicated, poorly
documented update procedure. Reimplementing it is risky (can leave the modem unusable).

## Options considered

1. Optional nRF Util for modem firmware only (chosen).
2. Out of scope for v2.0.
3. Own implementation on J-Link.

## Decision

Modem firmware updates call Nordic's **`nrfutil`** (device command) as an external process,
**only for this operation**, and only if the user has it installed. Everything else uses J-Link.
The app detects nRF Util (path discovery + manual override), checks its version, uses its JSON
output, and the setup/flash wizard explains how to install it when a modem update is requested.

## Consequences

- nRF Util is an optional runtime dependency; it is never bundled or committed.
- `DeviceSession` must release the probe (detach RTT, close J-Link) while nRF Util runs and
  re-attach afterwards.
- The subprocess wrapper lives in its own module (`libs/hw/nrfutil`) with bounded output,
  timeout and cancellation; errors are mapped to `hio::Error` like any other.
