# ADR-0183 — Device text is sanitised for terminals once, in libs/device

- **Status:** Accepted
- **Date:** 2026-10-29
- **Decided by:** Matej Strnad

## Context
Review M-1: `LineAssembler` strips only `ESC [` CSI. OSC, DCS, two-byte ESC sequences, C0 controls, DEL and 8-bit
C1 (U+009B acts as CSI) reach the CLI terminal. BLE advertised names (L-19) and PIB strings are not filtered at all.

## Options considered
1. Sanitise in each front end. 2. One sanitizer in `libs/device`, applied where device text enters the backend.

## Decision
Option 2. `hwio::device::sanitizeDeviceText()` strips CSI, OSC (to BEL/ST), DCS/SOS/PM/APC (to ST) and other
two-byte ESC sequences, drops C0 except TAB, drops DEL, and after lossy UTF-8 decoding replaces U+0080–U+009F with
U+FFFD. Used by `LineAssembler::makeLine` (after carriage-return handling), `BleScanner::add` and PIB string decoding.

## Consequences
GUI, CLI, console files and MCP see identical text. Device colour codes were already stripped; nothing visible is
lost. Raw bytes remain available only in the J-Link/BLE adapters.
