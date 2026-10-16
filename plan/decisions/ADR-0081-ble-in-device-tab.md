# ADR-0081 — Bluetooth is a connection type of a device tab; continuous scan in the picker

- **Status:** Accepted (supersedes the "BLE added in M5" rail item of ADR-0043; amends ADR-0050)
- **Date:** 2026-10-10
- **Decided by:** Matej Strnad (M5 planning session)

## Context

ADR-0043 planned BLE as an extra rail page in every tab. A tab, however, owns one connection to one device, and
its Console page already shows a shell. The legacy app had a separate Bluetooth page with a scan list (5 s per
click, RSSI sort, name filter) and its own shell.

## Options considered

1. **The "Choose device" picker offers J-Link | Bluetooth; a BLE tab shows the Console page (chosen).**
2. A separate BLE rail page in every tab, independent of the tab's J-Link session.
3. A global BLE page like Settings.

Scan: **continuous while the picker is visible (chosen)** / a manual Scan button with a 5 s scan.

## Decision

- The Choose-device page gets a segmented switch **J-Link | Bluetooth** (remembered in `ui/lastTransport`). The
  Bluetooth view lists nearby devices and has a Connect button; the device (profile) picker stays.
- A BLE tab shows the **Console** page with the log pane hidden (NUS carries the shell only, ADR-0082) and the
  **Flash** rail item disabled with the tooltip "Flashing needs a J-Link." The console Reset action is hidden
  (`ConsoleSession::capabilities`).
- Scan runs while a Bluetooth picker is visible: restarted every 10 s, rows sorted by RSSI with a signal icon,
  rows not seen for 15 s are dimmed ("not seen recently") and removed after 60 s, a name filter field, a
  "Show all devices" toggle (default off = profile filter, ADR-0083). Exactly one matching device → preselected.
- Choosing a device already open in another tab switches to that tab; one used by another process is disabled
  (ADR-0083).
- Tab title: `<profile> · <device name>` (e.g. `CHESTER · CHESTER-ABC123`) with the Bluetooth icon.

## Consequences

- No extra rail page; ADR-0043's rail stays Console + Flash.
- The same BLE list is reused by the BlePairing wizard (Embedded step `ble-picker`).
- A tab cannot hold a J-Link and a BLE connection at the same time; switching means a new tab.
