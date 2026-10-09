# 0001 — Architecture

- **Status:** Approved (high level, bootstrap 2026-10-09). Each module gets a detailed feature plan.
- **Related ADRs:** 0001–0034

## Layering

```
                ┌───────────────────────────┐    ┌──────────────────────────┐
                │ app/gui  (Qt Quick / QML) │    │ app/cli (headless)       │
                │ ViewModels, wizards       │    │ commands, MCP over stdio │
                └─────────────┬─────────────┘    └────────────┬─────────────┘
                              │   public backend API only     │
  ┌───────────────────────────┴───────────────────────────────┴──────────────┐
  │ libs/mcp      MCP protocol, tool registry, HTTP + stdio transports         │
  │ libs/device   DeviceProfile (CHESTER), Session, flash workflow, PIB/UICR   │
  │ libs/firmware firmware catalog client, download cache                      │
  ├────────────────────────────────────────────────────────────────────────────┤
  │ libs/hw/jlink     runtime-loaded JLinkARM: RTT transport + Programmer        │
  │ libs/hw/nrfutil   optional nRF Util subprocess (nRF91 modem firmware only)  │
  │ libs/hw/ble       Nordic UART Service transport (QtBluetooth)               │
  ├────────────────────────────────────────────────────────────────────────────┤
  │ libs/core   hio::Error, Result, logging, settings, bounded buffers, utils   │
  └────────────────────────────────────────────────────────────────────────────┘
```

Dependencies point only downward. `libs/*` link QtCore (+ QtNetwork / QtBluetooth where needed)
and never QtGui/QtQuick (ADR-0004). Each lib is a separate CMake target with its own tests.

## Proposed source layout

```
CMakeLists.txt, CMakePresets.json, cmake/        build system, toolchains, CPM
libs/core/            include/hio/core/...  src/  tests/
libs/hw/jlink/
libs/hw/nrfutil/
libs/hw/ble/
libs/device/
libs/firmware/
libs/mcp/
app/gui/              main.cpp, viewmodels/, qml/, assets/ (LFS)
app/cli/              CLI11 command tree (plan/0006-cli.md)
tests/integration/    hardware-in-the-loop, opt-in
packaging/            linux/ (AppImage, flatpak, deb, udev), macos/, windows/
plan/
```

## Key abstractions (to be refined in feature plans)

- `hio::Error` — `{ ErrorCode code; std::string message; std::string cause; std::string suggestion;
  std::string detail; std::optional<WizardId> wizard; }`; `hio::Result<T> = std::expected<T, Error>`.
  One `ErrorCode` enum with stable string ids, each mapped to an `ErrorCategory` that is also the
  CLI exit code (ADR-0024).
- `IJLinkApi` / `JLinkLibrary` — runtime-loaded DLL with typed function table from our own declarations
  (ADR-0028), minimum version 8.00 (ADR-0029); **each session loads its own copy** of the library so several
  probes work in one process (ADR-0030); load errors are `hio::Error` with "install J-Link" wizard.
- `IProgrammer` — `connect`, `erase`, `program(path, progress, stopToken)`, `verify`, `reset`,
  `recover`, `readUicr`… Implementation: J-Link library (ADR-0015). Modem firmware:
  `IModemUpdater` implemented by the nRF Util wrapper (ADR-0016).
- `IConsoleTransport` — byte/line channels (shell, log) with open/close/reconnect;
  implementations: J-Link RTT, BLE NUS (and serial in the future).
- `DeviceProfile` — compiled-in C++ data (ADR-0032) describing a device: MCU(s) and J-Link device names, expected family,
  RTT buffer names (`Terminal`, `Logger`), UICR/PIB layout, wizard illustrations,
  firmware catalog endpoints.
- `DeviceSession` — the single owner of a probe connection; serializes console, flash and
  modem-update operations (releases the probe while nRF Util runs) (no more flashing while RTT is attached — the session detaches, flashes,
  re-attaches automatically).

## Threading

- Each `DeviceSession` runs on its own owned `std::jthread`; all vendor-DLL calls for that probe happen
  there. Commands and events travel through bounded queues; a `QObject` façade on the owner thread emits
  batched queued signals (ADR-0031). GUI thread never blocks on hardware.
- Results/events travel to the UI via queued Qt signals; MCP and CLI use the same events.
- Every long operation takes a `std::stop_token` and has a timeout.
- Threads are owned (`std::jthread` / owned `QThread`) and joined on shutdown.

## Robustness patterns (taken from py-hardwario / rttt 1.9.2, ADR-0034)

- RTT buffers resolved by **name** on every attach; buffer counts polled every 100 ms (15 s deadline);
  a zero-sized terminal buffer (stale control block after reflash) → RTT stop and search again;
  logger buffer awaited at most 3 s, then continue without logs. RTT stop is called on session stop
  (supersedes the earlier "never call RTT stop" rule).
- Read failures report Disconnected but reading continues; only data or a successful write reports
  Connected. Writes are chunked with a 2 s timeout (never an infinite loop). Target voltage checked
  every 1 s; automatic reconnect every 3 s only while the target is powered; reset re-attaches.
- Cable diagnosis on errors: probe reachable → "check flat cable to device"; probe not reachable
  → "check USB cable to computer"; low voltage → "check power supply".
- Verify after every program; detected MCU family is checked against the profile.

## Data and persistence

- Settings: `QSettings` INI format on all OSes (versioned schema, migrations; ADR-0025).
- Logs, history, downloaded firmware: under `QStandardPaths::AppDataLocation`, size-bounded
  with rotation; an application diagnostic log (for support, QLoggingCategory + own bounded sink, ADR-0020) separate from device logs.
- Line storage for the console views: bounded ring buffer in the backend, model exposed to QML.

## Localization (ADR-0027)

English and Czech (more languages = a new `.ts` file + one table entry). Qt Linguist with English source
strings; backend texts are translated where they are created (`QCoreApplication::translate`, QtCore);
the GUI switches language live; the CLI is English only. Error `code` ids are the stable machine contract.

## Error and UX contract

Backend returns `hio::Error`; ViewModels turn it into a notification or an inline error panel
with "What to do" text and an optional "Help me fix this" button opening the wizard.
