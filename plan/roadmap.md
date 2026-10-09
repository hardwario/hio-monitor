# Roadmap

Each milestone is broken into feature plans in `features/`. A milestone starts only after its
plans are Approved. Every milestone also delivers the CLI commands for its features
(see [0006-cli.md](0006-cli.md)).

| Milestone | Content | Status |
|---|---|---|
| **M0 — Skeleton** | CMake + presets, toolchains, CPM, CI on 3 OSes, sanitizers, clang-tidy/format; `libs/core`: `hio::Error`, `Result`, logging, settings, bounded buffers; empty GUI app; CLI skeleton (CLI11, `--version`, `--json`, error/exit-code rendering) | Not planned |
| **M1 — J-Link & RTT** | runtime loading of JLinkARM, probe discovery, `DeviceSession`, RTT transport (find by name, reconnect, reset detection), CHESTER profile; CLI `chester app console` / `command` | Not planned |
| **M2 — Flashing** | hardware spike (ADR-0015), `IProgrammer` on J-Link, program/erase/reset/recover/UICR workflows (app + LTE), nRF Util modem update (ADR-0016), firmware catalog download + cache; CLI `chester app/lte flash/erase/reset`, `pib/uicr read` | Not planned |
| **M3 — GUI core** | navigation, console page (shell + log), search/filter/history/batch, flash page, settings, themes | Not planned |
| **M4 — Wizards & error UX** | first-run setup, connect device, flash, troubleshooting, diagnostic report | Not planned |
| **M5 — Bluetooth LE** | NUS transport, scan/connect UI, pairing help; CLI `ble scan` / `ble console` | Not planned |
| **M6 — MCP** | `libs/mcp`, HTTP transport in GUI, CLI `mcp` (stdio), tool set, consent UI | Not planned |
| **M7 — Packaging & release** | AppImage, Flatpak, .deb, DMG, Windows installer, signing, update check | Not planned |
| **M8 — Parity & retirement** | parity checklist in 0002 complete, beta with users, old app retired | Not planned |
