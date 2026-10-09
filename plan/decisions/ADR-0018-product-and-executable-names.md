# ADR-0018 — Product, executable and application identifiers

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M0 planning session)

## Context

open-questions #5: the final product name, executable names and bundle/app ids were open. The old
app is "HARDWARIO Monitor" / `hmonitor`; py-hardwario installs the `hardwario` command. During the
transition users may have the old app and py-hardwario installed side by side with v2.

## Options considered

- CLI: `hardwario` (true drop-in, but clashes on PATH with py-hardwario), `hio`, `hio-monitor-cli`,
  `hardwario-monitor`.
- GUI: `hio-monitor`, `hmonitor` (clashes with the old app), `hio-monitor2` (ages badly).

## Decision

| Item | Value |
|---|---|
| Product (display) name | **HARDWARIO Monitor** |
| GUI executable | **`hio-monitor`** |
| CLI executable | **`hio`** — same command tree and options as py-hardwario (`hio chester app flash …`) |
| Reverse-DNS application id | **`com.hardwario.monitor`** (Flatpak, macOS bundle id, Windows AppUserModelID) |
| Qt organization / application name | `HARDWARIO` / `Monitor`; organization domain `hardwario.com` |

## Consequences

- py-hardwario and v2 can be installed together; scripts migrate by replacing `hardwario` with `hio`.
- Settings and app data paths derive from the Qt names (e.g. `~/.config/HARDWARIO/Monitor.ini`).
- The ids are hard to change after the first release; packaging (M7) uses them as-is.
