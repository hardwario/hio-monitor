# ADR-0087 — BLE test seam: `IBleApi` with a scenario-driven fake backend

- **Status:** Accepted
- **Date:** 2026-10-10
- **Decided by:** Matej Strnad (M5 planning session)

## Context

The J-Link stack is tested end to end without hardware through a fake shared library loaded at runtime
(`tests/fake_jlink`, JSON scenarios). QtBluetooth is linked, not runtime-loaded, so that trick does not apply. CI
machines have no Bluetooth adapter.

## Options considered

1. **`IBleApi` hides QtBluetooth; a JSON-scenario fake backend selected by an environment variable, compiled only
   when test fakes are enabled (chosen).**
2. The same fake always compiled in (including release builds).
3. GMock only, no CLI end-to-end BLE tests.

## Decision

- `libs/hw/ble` defines `IBleApi` (adapter state, scan, connect/discover/notify, write, disconnect, pair, MTU) and
  `IBleListener` (events). `QtBleApi` implements it with QtBluetooth; everything above it (scanner, `NusLink`,
  `BleSession`) depends only on `IBleApi`.
- `FakeBleApi` reads a JSON scenario from `HIO_FAKE_BLE_SCENARIO` (adapter missing/off/permission, devices with
  name/RSSI timelines, connect delays and failures, MTU, write type, scripted responses keyed by command, prompt,
  pairing outcomes, link drops, write stalls). It runs on the BLE thread with `QTimer`s.
- It is compiled into `hio_hw_ble` only when the CMake option **`HIO_TEST_FAKES`** is ON (default = `HIO_BUILD_TESTS`;
  forced OFF by every `*-release` preset). Release binaries ignore the variable; a configure check fails a release
  preset that enables it.
- GMock (`MockBleApi`) is used for unit tests of `NusLink` and `BleSession`; HIL tests with a real CHESTER are
  opt-in (`HIO_HIL=1`, as M1).

## Consequences

- CLI end-to-end tests (`ble scan|console|command|pair`) run on all CI hosts.
- `QtBleApi` itself is covered only by HIL and manual checks on the three OSes.
