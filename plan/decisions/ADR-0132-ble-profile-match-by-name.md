# ADR-0132 — A BLE device matches a profile by its advertised name only

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad

Amends the filter part of ADR-0083 ("name pattern **or** advertised NUS UUID").

## Context

On a real adapter (2026-10-19), Ruuvi tags and other non-CHESTER devices advertise the Nordic UART Service UUID, so
`hio ble scan` listed them under the `chester` profile. NUS is a generic Nordic service, not a CHESTER identifier.

## Options considered

1. **Profile match by the name pattern only; NUS-only devices are shown with `--all` / "Show all devices" as
   unknown (chosen).**
2. Keep "name pattern or NUS UUID" (ADR-0083).

## Decision

`ProfileRegistry::findByAdvertisement` matches `BleProfile::namePattern` only. `nusAdvertised` stays in the scan
data (JSON field and the GUI row) for information. A device without a matching profile appears only with `--all`
(CLI) or "Show all devices" (GUI, 0012), with `profile: null`.

## Consequences

- The default list shows only CHESTER devices again (as the legacy app did by name).
- A CHESTER whose name was changed to something without `CHESTER` is found only with `--all`; connecting to it then
  needs an explicit `--profile`.
