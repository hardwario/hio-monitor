# 0001 — Architecture

- **Status:** Approved (high level, bootstrap 2026-10-09). Each module gets a detailed feature plan.
- **Related ADRs:** 0001–0017

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

- `hio::Error` — `{ ErrorCode code; std::string userMessage; std::string detail;
  std::string suggestion; std::optional<WizardId> wizard; }`; `hio::Result<T> = std::expected<T, Error>`.
- `IProbeLibrary` / `JLinkLibrary` — runtime-loaded DLL with typed function table; load errors
  are `hio::Error` with "install J-Link" wizard.
- `IProgrammer` — `connect`, `erase`, `program(path, progress, stopToken)`, `verify`, `reset`,
  `recover`, `readUicr`… Implementation: J-Link library (ADR-0015). Modem firmware:
  `IModemUpdater` implemented by the nRF Util wrapper (ADR-0016).
- `IConsoleTransport` — byte/line channels (shell, log) with open/close/reconnect;
  implementations: J-Link RTT, BLE NUS (and serial in the future).
- `DeviceProfile` — data describing a device: MCU(s) and J-Link device names, expected family,
  RTT buffer names (`Terminal`, `Logger`), UICR/PIB layout, wizard illustrations,
  firmware catalog endpoints.
- `DeviceSession` — the single owner of a probe connection; serializes console, flash and
  modem-update operations (releases the probe while nRF Util runs) (no more flashing while RTT is attached — the session detaches, flashes,
  re-attaches automatically).

## Threading

- Each `DeviceSession` runs on its own worker thread; all vendor-DLL calls for that probe happen
  there. GUI thread never blocks on hardware.
- Results/events travel to the UI via queued Qt signals; MCP and CLI use the same events.
- Every long operation takes a `std::stop_token` and has a timeout.
- Threads are owned (`std::jthread` / owned `QThread`) and joined on shutdown.

## Robustness patterns (taken from py-hardwario / rttt)

- RTT buffers found by **name**, control block polled (100 × 100 ms), DLL `RTT stop` not called
  (it prevents re-discovery until target reset).
- Target reset detection and automatic RTT reconnect; tolerate transient read errors
  (bounded retry count) before reporting.
- Cable diagnosis on errors: probe reachable → "check flat cable to device"; probe not reachable
  → "check USB cable to computer"; low voltage → "check power supply".
- Verify after every program; detected MCU family is checked against the profile.

## Data and persistence

- Settings: `QSettings` (versioned schema, migrations).
- Logs, history, downloaded firmware: under `QStandardPaths::AppDataLocation`, size-bounded
  with rotation; an application diagnostic log (for support) separate from device logs.
- Line storage for the console views: bounded ring buffer in the backend, model exposed to QML.

## Error and UX contract

Backend returns `hio::Error`; ViewModels turn it into a notification or an inline error panel
with "What to do" text and an optional "Help me fix this" button opening the wizard.
