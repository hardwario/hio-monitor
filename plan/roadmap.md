# Roadmap

Each milestone is broken into feature plans in `features/`. A milestone starts only after its
plans are Approved. Every milestone also delivers the CLI commands for its features
(see [0006-cli.md](0006-cli.md)).

| Milestone | Content | Status |
|---|---|---|
| **M0 — Skeleton** | CMake + presets, toolchains, CPM, CI on 3 OSes, sanitizers, clang-tidy/format; `libs/core`: `hio::Error`, `Result`, logging, settings, bounded buffers; empty GUI app; CLI skeleton (CLI11, `--version`, `--json`, error/exit-code rendering); translation infrastructure EN + CS (ADR-0027) | Approved — [features/0001](features/0001-m0-skeleton.md) |
| **M1 — J-Link & RTT** | runtime loading of JLinkARM (per-session copies), probe discovery, `DeviceSession`, RTT transport (find by name, reconnect, reset/power-loss detection), CHESTER profile; CLI `chester app console` / `command`, `jlink list` / `info` / `update-firmware` | Planned — [features/0002](features/0002-m1-jlink-rtt.md) (Draft) |
| **M2 — Flashing** | hardware spike (ADR-0015), `IProgrammer` on J-Link (ADR-0035), program/erase/reset/recover workflows (app + LTE), UICR rules (ADR-0036), nRF Util modem update (ADR-0016, 0040, 0042), firmware download by ID + bounded cache (ADR-0037); CLI `chester app/lte flash/erase/reset/recover`, `pib/uicr read` | Planned — [features/0003](features/0003-m2-flashing.md) (Draft) |
| **M3 — GUI core** | device tabs, navigation, console page (shell + log), search/filter/history/batch, flash page, settings (incl. language), themes; design system + mockups in [gui/](gui/design-system.md) | Planned — [features/0004](features/0004-m3-gui-core.md) (Draft) |
| **M4 — Wizards & error UX** | first-run setup, connect device, flash, troubleshooting, diagnostic report | Not planned |
| **M5 — Bluetooth LE** | NUS transport, scan/connect UI, pairing help; CLI `ble scan` / `ble console` | Not planned |
| **M6 — MCP** | `libs/mcp`, HTTP transport in GUI, CLI `mcp` (stdio), tool set, consent UI | Not planned |
| **M7 — Packaging & release** | AppImage, Flatpak, .deb, DMG, Windows installer, signing, update check | Not planned |
| **M8 — Parity & retirement** | parity checklist in 0002 complete, beta with users, old app retired | Not planned |
