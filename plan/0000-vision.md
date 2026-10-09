# 0000 — Vision

- **Status:** Approved (bootstrap, 2026-10-09)

## Why a rewrite

The current HARDWARIO Monitor (branch `main`) is unreliable: thread leaks, data races on the
J-Link library, silent failures, unbounded buffers, broken paths on Linux/macOS, no tests and
CI only on release (details: [0002-legacy-inventory.md](0002-legacy-inventory.md)).
The Python tooling `py-hardwario` does flashing and RTT much more reliably, but is a CLI for
developers. v2 brings that reliability to a desktop app that is easy to use for anyone.

## Goals

1. **Stability** — no crashes, no hangs, no silent failures. Every hardware operation can be
   cancelled, has a timeout and reports its outcome.
2. **User-friendliness** — clear navigation; **wizards** that walk users through things they
   don't know ("connect the J-Link flat cable to connector X", "power the device", "install
   J-Link software").
3. **Much better error messages** — every error says what happened, the likely cause, and what
   to do next, and can open the matching troubleshooting wizard.
4. **Memory safety** — modern C++23, RAII everywhere, sanitizers and static analysis in CI.
5. **Modularity** — backend libraries fully independent of the UI; a headless CLI on the same backend.
6. **AI integration** — built-in MCP server so AI assistants can read logs, send shell
   commands, flash and reset devices (with user consent).
7. **Longevity** — the app will be maintained for many years: pinned toolchains, LTS Qt,
   documented decisions (ADRs), high test coverage of the backend.
8. **Cross-platform** — Linux, Windows, macOS with the same features.

## v2.0 scope

- J-Link RTT console (shell) and device log, with search, filter, history, batch commands.
- Bluetooth LE shell (Nordic UART Service).
- Flashing (local file or firmware ID from the HARDWARIO firmware catalog), erase, reset.
- Device: **CHESTER** (nRF52840 application MCU, nRF91 LTE MCU), through an extensible
  device-profile abstraction.
- Setup / connect / flash / troubleshooting wizards.
- CLI compatible with py-hardwario's `hardwario` command (human + `--json` output).
- MCP server (localhost HTTP in the GUI, stdio in the CLI).
- Installers for Linux (AppImage, Flatpak, .deb), macOS (DMG), Windows.

## Non-goals for v2.0 (may come later via new plans)

- Serial/UART console.
- Non-CHESTER devices and non-Nordic MCUs (architecture must allow adding them).
- Firmware upload to the catalog, PIB/UICR writing for production (read may be in scope).
- Python at runtime.

## Success criteria

- Every feature of the old app has an equivalent (parity checklist in 0002) and the old app
  can be retired.
- Zero known crashes; sanitizer-clean test suite; CI green on all three OSes for every PR.
- A first-time user can go from download to a working RTT console using only the wizards.
