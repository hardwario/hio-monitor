# ADR-0079 — `ConsoleSession` interface shared by J-Link and BLE sessions

- **Status:** Accepted (amends feature 0002, feature 0005 and the `IConsoleTransport` bullet of 0001-architecture)
- **Date:** 2026-10-10
- **Decided by:** Matej Strnad (M5 planning session)

## Context

The architecture names an `IConsoleTransport` with RTT and BLE NUS implementations, but M1 built `DeviceSession`
directly on `JLinkProbe` + `RttChannel`. Its options, registry slot, lock and operations (flash, erase, reset,
diagnose — ADR-0057, ADR-0062, ADR-0069) are J-Link specific. The consumers of a console — the CLI
`console`/`command` (`ConsoleView`, `runConsole`, `waitUntilConnected`), the GUI `ConsoleViewModel` and later MCP —
only need the console part.

## Options considered

1. **An abstract `ConsoleSession` QObject implemented by `DeviceSession` and a new `BleSession` (chosen).**
2. Refactor `DeviceSession` around an `IConsoleTransport` (RTT, NUS): one session class, J-Link operations become
   "not supported" for BLE.
3. A fully separate `BleSession` with BLE-specific console code paths in the CLI and GUI.

## Decision

- `libs/device` gains `hio::device::ConsoleSession : QObject` (abstract): `sendLine`, `reconnect`, `close`, `log`,
  `connection`, `identity`, `capabilities`, `profile`, `consoleTargetId`, `consoleFilePath`, and the signals
  `linesReceived`, `connectionChanged`, `linesDropped`, `warning`. Exact signatures are in feature 0011.
- `DeviceSession` derives from `ConsoleSession`; its J-Link operations, `suspend/resume`, `probe()`, `diagnose` and
  `startConnection` stay on `DeviceSession` only. `BleSession` derives from `ConsoleSession` and adds `pair()` and
  `rssi()`.
- `capabilities()` (`Reset`, `LogChannel`, `Operations`) lets the UI hide what a transport cannot do instead of
  failing.
- `waitUntilConnected`, `waitForFirstAttempt`, the CLI `ConsoleView` / `runConsole` / command collector and the
  GUI `ConsoleViewModel` take `ConsoleSession&`. `DeviceTabViewModel` gains `consoleSession()`; `session()` keeps
  returning the J-Link `DeviceSession*` (nullptr for BLE tabs).
- The internal worker of each session stays transport specific; there is no shared `IConsoleTransport` class. The
  shared line pipeline (`LineAssembler`, `ConsoleLog`, `CommandHistory`, `ConsoleFileSink`) is reused as is.

## Consequences

- Small, mechanical amendment to M1 (a base class and changed parameter types); M1 tests keep passing unchanged.
- A future serial console is a third `ConsoleSession` implementation.
- The `IConsoleTransport` bullet of 0001-architecture is replaced by this interface.
