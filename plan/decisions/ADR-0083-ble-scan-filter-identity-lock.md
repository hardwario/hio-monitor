# ADR-0083 — BLE scan filter, device identity, cross-process lock and tab restore

- **Status:** Accepted (extends ADR-0058; amends ADR-0061); filter amended by ADR-0132 (name pattern only)
- **Date:** 2026-10-10
- **Decided by:** Matej Strnad (M5 planning session)

## Context

The legacy app kept devices whose name contains "CHESTER" and de-duplicated them **by name**, so two devices with
the same name collapsed into one row. macOS (CoreBluetooth) hides Bluetooth addresses and gives each peripheral a
per-host UUID instead. J-Link probes are locked across processes by serial number (ADR-0058) and restored tabs
store `{profile, target, sn}` (ADR-0061).

## Options considered

- Filter: **profile name pattern or advertised NUS UUID (chosen)** / name only / NUS UUID only.
- Identity and lock: **address/UUID + cross-process lock + tab restore (chosen)** / no lock / no tab restore.

## Decision

- **Filter:** a device matches a profile when its advertised name matches `BleProfile::namePattern` (CHESTER:
  case-sensitive substring `CHESTER`) **or** it advertises the NUS service UUID. `hio ble scan --all` and the GUI
  "Show all devices" toggle list every BLE device.
- **Identity** (`BleDeviceId`, a string): the Bluetooth address `AA:BB:CC:DD:EE:FF` on Linux and Windows, the
  CoreBluetooth UUID on macOS (stable on one Mac only). Rows are de-duplicated by identity, never by name.
- **Lock:** `BleLock` (RAII, same rules as `ProbeLock`): `QLockFile <AppLocalData>/locks/ble-<id>.lock` (`:` and
  `{}` replaced by `-`), 0 ms wait, stale detection by PID; held → `ble.in_use_by_other_process` with app name and
  PID.
- **Registry:** `SessionRegistry` keys become `SessionKey = std::variant<ProbeKey /*sn*/, BleKey /*id*/>`; one session
  per key; J-Link and BLE sessions share the limit of **8**.
- **Tab restore:** `ui/tabs` entries gain `transport` (`"jlink"` default when absent, `"ble"`); a BLE entry is
  `{transport:"ble", profile, id, name}`. Restored BLE tabs start Disconnected and never connect automatically
  (ADR-0061). A missing device → `ble.device_not_found` with "Choose another device".

## Consequences

- Restored BLE tabs work on the same computer; on another Mac the UUID differs and the user chooses again.
- The settings schema version is bumped by 0012 with a no-op migration (absent `transport` = `jlink`).
