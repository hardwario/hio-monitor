# ADR-0122 — The hio CLI ships with the GUI in every package and is on PATH

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M7 planning session)

## Context

ADR-0106 tells users to configure MCP clients with `<path>/hio mcp`; the CLI is also used in scripts and CI. The old
app had no CLI; py-hardwario is a pip package.

## Options considered

1. **Ship `hio` inside every GUI package and put it on PATH (chosen).**
2. Separate CLI packages/archives.
3. Both.

## Decision

- **.deb:** `/usr/bin/hio` (symlink into `/opt/hardwario-monitor/bin`).
- **MSI:** `hio.exe` next to `hio-monitor.exe`; the install directory is added to the system PATH (ADR-0117).
- **macOS:** `hio` in `HARDWARIO Monitor.app/Contents/MacOS/hio`, signed with the bundle and using the bundle's Qt
  frameworks. Its embedded `__info_plist` stays (ADR-0086). Settings › About shows the full path with [Copy] and the
  command to create `/usr/local/bin/hio` as a symlink; the app never creates it itself.
- **AppImage:** one AppImage; started as `hio-monitor-<v>-x86_64.AppImage hio <args>` or through a symlink named `hio`
  (argv[0] dispatch in AppRun) it runs the CLI, otherwise the GUI. The About page shows the command.
- The About page and the MCP settings page (0014) show the real CLI path for the current installation.

## Consequences

- No separate CLI artifacts in M7; headless CI users take the AppImage, the .deb or the MSI.
- The macOS CLI needs the bundle; copying `hio` out of it does not work (documented).
