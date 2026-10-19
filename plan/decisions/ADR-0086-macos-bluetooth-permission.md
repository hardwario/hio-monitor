# ADR-0086 — macOS Bluetooth permission: Info.plist for the GUI bundle and embedded in the CLI

- **Status:** Accepted
- **Date:** 2026-10-10
- **Decided by:** Matej Strnad (M5 planning session)

## Context

macOS (TCC) terminates or blocks a process that uses CoreBluetooth without `NSBluetoothAlwaysUsageDescription` in
its Info.plist. The GUI bundle currently uses the plist that CMake generates (no usage description); the legacy app
had a custom `ci/macos/Info.plist` with the key. The `hio` CLI is a bare executable without a bundle.

## Options considered

1. **Custom GUI `Info.plist.in` + an Info.plist embedded into the CLI binary (chosen).**
2. BLE commands unsupported in the CLI on macOS.
3. Ship the CLI inside the app bundle (packaging question for M7).

## Decision

- `app/gui/macos/Info.plist.in` (set through `MACOSX_BUNDLE_INFO_PLIST`) keeps the current identifiers
  (`com.hardwario.monitor`, versions) and adds `NSBluetoothAlwaysUsageDescription` = "HARDWARIO Monitor uses
  Bluetooth to connect to the console of your devices." (English; macOS shows it as is).
- `app/cli/macos/Info.plist.in` (`CFBundleIdentifier` `com.hardwario.monitor.cli`, same usage description) is
  linked into `hio` as the `__TEXT,__info_plist` section (`-sectcreate`). macOS attributes the permission to the
  responsible process (usually the terminal app).
- Authorization state is read through QtBluetooth/CoreBluetooth before scanning: denied or restricted →
  `ble.permission_denied` (wizard FirstRunSetup, which opens at its Bluetooth step, ADR-0088) with the suggestion
  "Allow Bluetooth for HARDWARIO Monitor (or your terminal) in System Settings → Privacy & Security → Bluetooth."
- A CTest checks on macOS that both binaries contain the key (`otool -s __TEXT __info_plist` / bundle plist).

## Consequences

- No extra entitlement is needed until hardened-runtime signing in M7, which must keep both plists.
- The first scan on macOS triggers the system prompt; the setup wizard's Bluetooth step (ADR-0088) triggers it in a
  controlled place.
