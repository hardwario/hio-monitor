# ADR-0099 — MCP tool set v1: rttt parity plus HARDWARIO tools; new low-level target operations

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (M6 planning session)

## Context

rttt 1.9.2 (`rttt/connectors/mcp_server.py`) is the reference tool set: send_command, read_terminal, read_log,
wait_for_state, wait_for_connection, status, flash, reconnect, stop, start, jlink_close, jlink_open, reset, halt,
go, target_status, read_memory, write_memory, write_flash, read_registers and memory_zones. Our backend has
reconnect, suspend/resume, reset (go/halt), readMemory, flash, erase and recover, but no halt/go without a reset,
no memory write, no registers and no memory zones.

## Options considered

1. **rttt parity plus ours, adding the missing backend operations (chosen).**
2. Leave out the low-level writes, registers and halt/go.
3. Read-only + console.

## Decision

Tools v1 (snake_case, rttt names kept where the semantics match; "D" marks consent categories, ADR-0101):

| Group | Tools |
|---|---|
| Discovery | `list_devices`, `ble_scan`, `connect` (D connect in GUI), `disconnect`, `status` |
| Console | `send_command`, `read_terminal`, `read_log`, `wait_for_state`, `wait_for_connection` |
| Session control | `reconnect`, `stop`, `start`, `release_probe` (= rttt `jlink_close`), `reacquire_probe` (= `jlink_open`) |
| Target | `target_status`, `reset`, `halt`, `go`, `read_memory`, `read_registers`, `memory_zones`, `read_pib`, `read_uicr`, `write_memory` (D memory-write), `write_flash` (D flash; UICR range → D uicr) |
| Firmware | `flash` (D flash; file or catalog id, ADR-0100; UICR records → D uicr), `flash_lte` (D flash; modem firmware / LTE bundle, ADR-0040), `erase` (D erase), `recover` (D erase) |
| Diagnostics | `diagnose` (ADR-0069), `doctor` (ADR-0068 environment checks) |

Not in v1: J-Link probe firmware update, BLE pairing (it needs the OS dialog; the result points to the BlePairing
wizard / `hio ble pair`), PIB/UICR write (open question 11), the firmware catalog list.

`libs/device` gains operations on the session worker (they are serialised like the others, and `internal.busy` is
returned while one runs): `halt`, `go`, `targetStatus` (halted, core id, J-Link device name), `writeMemory`
(≤ 64 KiB, width 8/16/32), `readMemory` with width, `readRegisters` (requires a halted core,
`target.not_halted` otherwise), `memoryZones`, and `writeFlash` (≤ 1 MiB raw bytes: reset+halt, programmer write
with erase, verify, reset, RTT re-attach; M2 range rules). The JLinkARM functions are added to the own declarations
(ADR-0028) and cross-checked against pylink 2.0.2.

Address checks: `TargetProfile` gains `ram` and `peripherals` ranges. Reads are allowed in flash, UICR, RAM and
peripherals. `write_memory` is allowed in RAM and peripherals only. `write_flash` is allowed in flash, and in UICR
only with `allow_uicr`. Anything else gives `target.address_out_of_range`.

## Consequences

- AI workflows from rttt keep working with renamed probe tools. The `debug_device` prompt explains the mapping.
- The new operations also get CLI commands (CLAUDE.md rule): `hio chester app|lte halt|go|status|regs|zones`,
  `mem read|write`, `flash-write`.
