# ADR-0078 — M5 is split into a backend + CLI plan and a GUI + pairing plan

- **Status:** Accepted
- **Date:** 2026-10-10
- **Decided by:** Matej Strnad (M5 planning session)

## Context

M5 adds Bluetooth LE (Nordic UART Service) to a code base whose sessions, registry, locks and profiles are J-Link
specific. It touches the backend (new `libs/hw/ble`, a console-session interface extracted from `DeviceSession`),
the CLI, `libs/diag`, the GUI shell (Choose-device picker, device bar, tab restore), the console page and two
wizards (a new BLE pairing wizard and a step in the setup wizard). Earlier milestones were split so that agents can
implement plans in parallel and each plan stays reviewable (ADR-0059, ADR-0065).

## Options considered

1. **Two plans: backend + CLI, then GUI + wizards (chosen).**
2. Three plans: backend + CLI; GUI scan/connect/console; pairing wizard + macOS permission + doctor checks.
3. One phased plan for all of M5.

## Decision

| Plan | Content | Depends on |
|---|---|---|
| **0011 — BLE backend and CLI** | `libs/hw/ble` (`IBleApi`, QtBluetooth backend, fake backend, `BleHost` thread, scanner, NUS link), `ConsoleSession` interface extracted from `DeviceSession` (ADR-0079), `BleSession`, `BleLock`, registry keys, `DeviceProfile::ble`, error codes, `bluetooth` doctor check, CLI `hio ble scan|console|command|pair`, macOS Info.plist for the GUI bundle and the CLI | M1 (0002), M4 0007 (`libs/diag`) for the doctor check only |
| **0012 — BLE GUI and pairing wizard** | Choose-device transport switch and BLE device list, BLE tabs (device bar, console page, tab restore), BlePairing wizard, troubleshooting topics for `ble.*` codes, Bluetooth step in the setup wizard, mockups | 0011, M3 0004/0005, M4 0007/0008/0009 |

## Consequences

- 0011 can start as soon as M1 is done; its CLI makes BLE usable and testable before any GUI work (0006-cli goal).
- The `ConsoleSession` extraction is done once, in 0011, and 0012 only consumes it.
- Shared QML components and `Theme` still change only through 0004 or an amendment to it (ADR-0059); 0012 is that
  amendment for the BLE parts of the shell.
