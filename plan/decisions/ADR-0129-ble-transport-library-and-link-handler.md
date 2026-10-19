# ADR-0129 — `libs/hw/ble` is transport only; console handling via a per-link handler from `libs/device`

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad

Amends plan 0011 (module contents of `libs/hw/ble`) and refines ADR-0128.

## Context

`libs/device` (`BleSession`) links `hio_hw_ble` (`BleHost`). Plan 0011 put two things into `libs/hw/ble` that need
`libs/device` types: `NusLink` owning `LineAssembler`, prompt detection and `ConsoleFileSink` (ADR-0128), and
`BleScanner` with `const DeviceProfile*` and a profile filter. That is a circular dependency.

## Options considered

1. **`hw/ble` transport only + per-link handler interface implemented in `libs/device` (chosen).**
2. Move `NusLink` and `BleScanner` into `libs/device`.
3. Split `ConsoleLine`, `LineAssembler`, `ConsoleFileSink` and the locks into a lower library.

## Decision

- `NusLink` stays in `libs/hw/ble` and does transport only: chunking, write modes, 2 s write timeout, pairing wait,
  connect deadline.
- `hio::ble::ILinkHandler` (per link): receives bytes and link events on the BLE thread. The handler is passed in the
  Connect command, owned and used only on the BLE thread.
- `libs/device` implements it: `LineAssembler`, prompt detection and `ConsoleFileSink` on the BLE thread
  (ADR-0128), pushing finished lines into the `BleSession`'s bounded event queue (10 000, `DropOldestMatching`, only
  data lines dropped) with a coalesced wake-up.
- `BleScanner` stays in `libs/hw/ble`; profile matching is injected (a matcher built from
  `ProfileRegistry::findByAdvertisement`); `BleDeviceInfo` carries `std::string profileId` (empty = none) instead of
  `const DeviceProfile*`.

## Consequences

- No circular dependency; `hw/ble` has no knowledge of profiles or console files.
- Threading is unchanged from ADR-0128.
