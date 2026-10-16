# ADR-0088 — Bluetooth checks in `hio doctor` and an optional Bluetooth step in the setup wizard

- **Status:** Accepted (amends feature 0007 and feature 0008)
- **Date:** 2026-10-10
- **Decided by:** Matej Strnad (M5 planning session)

## Context

0007's environment checks cover J-Link, nRF Util, USB access, probe holders and folders. The macOS Bluetooth
permission step was deferred from 0008 to M5. Users without an adapter, with Bluetooth off, or with a denied
permission currently learn about it only when a scan fails.

## Options considered

- **`hio doctor` check `bluetooth` (chosen).**
- **Optional step in the first-run setup wizard (chosen).**
- Checks only inside the BlePairing wizard.

## Decision

- `libs/diag` gains the check `bluetooth` (title "Bluetooth"), run after `app-dirs`:
  - Ok: "Adapter ready" (+ adapter name/address where available);
  - Warning (never Failed — BLE is optional): `ble.adapter_not_found`, `ble.adapter_off`, `ble.permission_denied`
    (macOS), `ble.stack_unavailable` (Linux: BlueZ not reachable on D-Bus);
  - the permission state is read without triggering the macOS prompt (`notDetermined` → Ok "Permission will be
    asked on first use").
- `hio doctor` therefore needs the `BleHost` (short-lived) in the CLI; the check's timeout is 5 s.
- The setup wizard gains a skippable **Bluetooth (optional)** step after nRF Util: shows the check; on macOS with
  `notDetermined` it offers [Allow Bluetooth access], which starts a 1 s scan to trigger the system prompt, then
  re-checks.

## Consequences

- 0007's check table and 0008's step list are amended (applied when 0011/0012 are approved).
- Doctor exit codes are unchanged (warnings do not change the exit code, ADR-0070).
