# ADR-0080 — One application-owned BLE thread with an event loop (`BleHost`)

- **Status:** Accepted (complements ADR-0031 for Bluetooth)
- **Date:** 2026-10-10
- **Decided by:** Matej Strnad (M5 planning session)

## Context

ADR-0031 runs each J-Link session on an owned `std::jthread` with an explicit loop and no event loop. QtBluetooth
(`QBluetoothDeviceDiscoveryAgent`, `QLowEnergyController`, `QLowEnergyService`, `QBluetoothLocalDevice`) is
asynchronous and needs a Qt event loop in the thread that owns its objects. The legacy app ran everything on the GUI
thread (its `QThread` include was unused).

## Options considered

1. **One application-owned `QThread` with an event loop hosting the scanner and every BLE link (chosen).**
2. An owned `QThread` per BLE session plus one for the scanner.
3. QtBluetooth on the owner thread (GUI / CLI main thread), relying on its asynchrony.

## Decision

- `hio::ble::BleHost` owns one `QThread` (started on first use, stopped and joined in its destructor / at
  application shutdown). All QtBluetooth objects are created, used and destroyed on that thread, with Qt parent
  ownership under one `BleWorker` QObject that is moved to the thread.
- The façades (`BleScanner`, `BleSession`) live on the owner thread. Communication uses the ADR-0031 pattern in
  both directions: a `BoundedQueue` of commands into the host and a `BoundedQueue` of events out of it, each side
  woken by one coalesced queued `QMetaObject::invokeMethod` with an atomic pending flag. Qt's own event queue never
  carries payloads, so all buffering stays bounded.
- No vendor code is involved; the "one owner thread per hardware session" rule holds per link: each link's objects
  are only touched on the BLE thread.
- Shutdown: the owner calls `BleHost::stop()` (disconnects every link, deletes the QtBluetooth objects on the BLE
  thread, quits the loop, joins with a 3 s bound, then logs and continues). `BleHost` is owned by the application
  object (GUI `AppViewModel`, CLI command), never a global static.
- Up to 8 links share the thread; per-link work is small (notifications and writes of ≤ 512 bytes).

## Consequences

- One place to reason about QtBluetooth threading on all three platforms (CoreBluetooth, BlueZ D-Bus, WinRT).
- A slow callback in one link delays the others; handlers must stay short (no file I/O on the BLE thread — the
  console file sink runs on the owner thread for BLE sessions).
- TSan covers the queues; QtBluetooth internals are exercised only in HIL.
