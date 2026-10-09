# 0002 — Legacy inventory and parity checklist

- **Status:** Approved (reference document)

Sources: old app (branch `main`, Qt 6.6 + QML) and `../py-hardwario` v1.7.2 (+ `rttt` 1.9.2,
which holds the RTT console / MCP logic and is not in that repo).

## Feature parity with the old app

| Old feature | v2 | Notes |
|---|---|---|
| RTT attach / detach (CHESTER nRF52840, no reset) | [ ] | + probe selection by serial number, speed setting |
| Interactive shell on RTT channel 0, echo highlight, error coloring | [ ] | use buffer names `Terminal` / `Logger` instead of indexes |
| Device log on RTT channel 1, level coloring `<dbg/inf/wrn/err>` | [ ] | |
| Log search (regex, highlight, next/prev, n / Shift+n) | [ ] | handle invalid regex gracefully |
| Log filter | [ ] | |
| Pause / resume, clear, autoscroll, 100 000-line view limit | [ ] | |
| Multi-line selection and copy | [ ] | |
| Command history (persisted, Up/Down, Ctrl+R search) | [ ] | |
| Batch commands from a text file (200 ms spacing) | [ ] | fix Linux/macOS path bug |
| Open log file | [ ] | |
| BLE scan (CHESTER filter, RSSI sort, search), connect, NUS shell | [ ] | |
| Flash local .hex | [ ] | |
| Flash by firmware ID from catalog (`https://firmware.hardwario.com/chester/<id>/hex`), cache | [ ] | |
| Flash log | [ ] | |
| Update check (GitHub releases) | [ ] | semantic version comparison |
| Doc / HARDWARIO links | [ ] | |

## Behaviour to replicate from py-hardwario

py-hardwario uses nrfjprog for programming. v2 reproduces the **behaviour** below with the
J-Link library instead (ADR-0015); modem firmware goes through nRF Util (ADR-0016).

**nrfjprog session** (`hardwario/device/nrfjprog.py`, `hardwario/chester/nrfjprog.py`):
open DLL (family UNKNOWN) → connect to emulator (by SN / without SN, speed) → map errors
(`NO_EMULATOR_CONNECTED` → "No J-Link found (check USB cable)", `LOW_VOLTAGE` → "check power
supply and cable") → `read_device_family` and check against expected (app = NRF52, lte = NRF91)
→ `select_family` → operations → close.

**Program:** `sys_reset` → `halt` → `erase_file(ERASE_SECTOR)` (UICR preserved) →
`program_file` → `verify_file` → `sys_reset` → `go` (or `halt`).
Progress phases: Erasing / Flashing / Verifying / Resetting / Done.

**Erase:** page-wise over CODE memory (keeps UICR) or `erase_all`. **Reset:** `sys_reset` + `go`/`halt`.
**Recover (nRF91):** `recover` (~30 s) → program `nrf91_disable_ap_protect.hex` (bare `program_file`, no erase/verify).
In py-hardwario this is used only by `device nrf91 pib write`; `chester` has no recover (v2 adds it, ADR-0039).

**LTE flash:** a `.zip` containing `modem.zip` + `application.hex` → program modem, then app (py-hardwario uses
nrfjprog's built-in modem DFU; v2: modem via nRF Util, app via J-Link; plain modem zips and `.hex` also accepted, ADR-0040).

**RTT (J-Link)** — as implemented in rttt 1.9.2 `PyLinkRTTConnector` (read for M1; v2 follows it, ADR-0034):
open (SN) → speed → SWD → connect device name → RTT start → poll buffer counts every 100 ms
(15 s deadline) → resolve buffers by name → wait for non-zero terminal (and logger, ≤ 3 s) sizes,
else RTT stop + restart search (stale control block) → read loop with ~50 ms latency, reading up to
the full buffer size; read failure → disconnected (reading continues), data → connected; write in
down-buffer chunks with 2 s timeout, echo only after the write; VTref checked every 1 s (no power →
disconnected); optional auto-reconnect every 3 s (reopen probe + connect + attach) only when powered;
RTT stop on session stop. rttt decodes with `backslashreplace`; v2 uses U+FFFD.
Lines starting with `#` on the terminal channel are log lines (old firmware without `Logger`).
J-Link firmware outdated → offer firmware update.

**Chip names:** strip suffix, map `NRF9120_xxAA` → `NRF9151_XXCA`.

**PIB v2** (big-endian, 128 B, UICR + 0x80 on nRF52, 0x00FF8108 on nRF91): signature
0xBABECAFE, version 2, size, vendor 17s, product 17s, hw variant 11s, hw revision 7s (`Rx.y`),
serial 11s, claim token 33s, BLE passkey 17s, CRC32 (reflected, 0xEDB88320) at size − 4.
Serial number check `(sn & 0xC0000000) == 0x80000000`, family `(sn >> 20) & 0x3FF`.

**Firmware catalog:** download `https://firmware.hardwario.com/chester/{id}/hex`, cache by id;
API `https://firmware.hardwario.com/chester/api/v1/firmware` (list/detail; upload/delete
need a token — out of v2.0 scope).

**MCP tools in rttt:** `send_command`, `read_log`, `wait_for_state`, `flash`, `reset`, `halt`,
`go`, `read_memory`, `write_memory`, `write_flash`, `read_registers`, `memory_zones`, `reconnect`.
→ obtain `rttt` sources for exact semantics before the MCP feature plan.

## Old-app defects that must not recur

- J-Link shared between flasher and RTT without ownership; double attach possible.
- Leaked threads (`QThread::create` never deleted), data races on command state, concurrent DLL calls.
- Infinite loop when RTT write returns 0; `QTimer` used in threads without an event loop.
- Silent failures: read-failure signals unhandled, detach always reports success, BLE send
  reports success before the write.
- Unbounded buffers (BLE message buffer, history rewrite on every command, unrotated logs).
- Error detection by matching "command not found" strings only.
- Windows-only path handling (`file://` not stripped on Linux/macOS).
- Null dereference in BLE discovery error handler; BLE devices de-duplicated by name.
- Downloader objects leaked and reconnected on each attempt; save errors ignored.
- HTML stored in the model and `QTextDocument` per line (slow).
- User search input used as raw regex.
- Version compared with `!=`; hard-coded `v0.0.0` patched by sed.
- ~380 MB of vendor binaries committed to git; no tests; CI only on release.
- Old app flashing: family hard-coded to NRF52 and never checked, first probe only, indeterminate progress,
  user had to detach RTT manually, catalog firmware re-downloaded every time without timeout or HTTP status check.

## py-hardwario defects that must not recur (found in M2 planning)

- `chester lte flash` with a zip other than the 2-entry bundle programs nothing and reports success.
- Group-level `chester app -n SN` overwritten with `None` by `flash`, `erase`, `reset`, `pib`, `uicr`.
- `chester app flash` without an argument crashes when no build output is found.
- Firmware cache: unchecked hits, partial downloads become cache hits, no HTTP timeouts.
