# Migrating from py-hardwario

[Documentation](index.md) › Migrating from py-hardwario

`hwio` replaces the `hardwario` command of [py-hardwario](https://pypi.org/project/hardwario/) (version 1.7.2) for
CHESTER. The commands and options have the same names, so most scripts keep working when you replace `hardwario`
with `hwio`:

```sh
hardwario chester app flash firmware.hex      # before
hwio chester app flash firmware.hex           # now
```

`hwio` needs no Python. It uses the SEGGER J-Link Software directly (no nrfjprog) and nRF Util only for the LTE modem
firmware. This page lists what is different.

## Behaviour that changed on purpose

- **Confirmations.** `chester app erase --all`, `chester lte erase` and the new `recover` commands ask `[y/N]` in a
  terminal. In scripts (no terminal) add `--yes`, otherwise they stop with exit code 2. `flash`, `erase` without
  `--all` and `reset` never ask.
- **UICR.** An application image that contains UICR records is flashed only with `--allow-uicr`. The Product
  Information Block is always kept, and an image that would overwrite it is rejected.
- **`chester lte flash` checks the file.** It looks at the content: a CHESTER LTE bundle (modem firmware and
  application), a Nordic modem firmware zip or an Intel HEX file. Any other zip is an error; py-hardwario silently did
  nothing.
- **`chester lte flash` without a file** downloads, verifies and flashes the HARDWARIO LTE firmware.
- **J-Link speed.** `--jlink-speed` is `auto` by default (py-hardwario: a fixed 2000 kHz for the application MCU). A
  number in kHz works as before.
- **`-n` on the group** (`hwio chester app -n SN flash …`) applies to every subcommand.
- **Files.** The command history and the console files are in the application data folder of HARDWARIO Monitor and
  are shared with the desktop app ([Console](console.md#console-files)).
- **JSON.** `--json` is a global option (`hwio --json chester app pib read`), and every result is an envelope
  `{"schema":1,"ok":…,"data"|"error":…}` ([Command line](cli.md#json-output)).
- **Exit codes** are documented and stable ([Command line](cli.md#exit-codes)).
- **Errors** say what happened, the probable cause and what to do (`Error:`, `Cause:`, `What to do:`).

## Commands

| py-hardwario | hwio |
|---|---|
| `hardwario --log-level L`, `--version` | the same |
| `chester app flash [HEX_FILE_OR_ID] [--halt]` | the same, plus `--allow-uicr`, `--no-cache` |
| `chester app erase [--all]` | the same, plus `--yes` |
| `chester app reset [--halt]` | the same |
| `chester app console [--reset] [--latency] [--history-file] [--console-file] [--auto-reconnect]` | the same, plus `--linger`, `--no-suggestions` |
| `chester app console --coredump-file FILE` | accepted, no effect |
| `chester app console --mcp`, `--mcp-listen`, `--mcp-token` | replaced by [`hwio mcp`](mcp.md) (`--http`, `--port`, `--token-file`) |
| `chester app command CMD [--reset] [-t] [--console-file]` | the same |
| `chester app pib read [--json]` | `hwio --json chester app pib read` |
| `chester app pib write …` | not in this version |
| `chester app uicr read --format {hex,bin} FILE` | the same, plus `-` for stdout and `--force` |
| `chester app uicr write …` | not in this version |
| `chester app fw upload\|list\|info\|delete` | not in this version; `chester app catalog-info ID` shows a catalog firmware |
| `chester lte flash FILE` | the same; FILE is optional |
| `chester lte erase` | the same, plus `--yes` |
| `chester lte reset` | the same |
| `chester lte trace …` | not in this version |
| `--nrfjprog-log` | not in this version (no nrfjprog) |
| `device nrf51\|nrf52\|nrf53\|nrf54h\|nrf54l\|nrf91 …` | not in this version |

## New in hwio

| Command | Does |
|---|---|
| `jlink list`, `jlink info`, `jlink update-firmware` | J-Link probes and their firmware |
| `chester app info` | What the device reports with `info show` |
| `chester app catalog-info ID` | A catalog firmware's name, version and checksum |
| `chester app recover`, `chester lte recover` | Remove read-back protection |
| `chester app check`, `chester lte check` | Check the connection step by step |
| `chester app\|lte status\|halt\|go\|regs\|zones\|mem\|flash-write` | Low-level target commands |
| `ble scan\|console\|command\|pair` | The console over Bluetooth LE ([Bluetooth LE](bluetooth.md)) |
| `console history clear` | Delete the command history |
| `doctor` | Check the installation |
| `diag report` | A diagnostic report for support |
| `mcp` | The MCP server for AI assistants ([MCP server](mcp.md)) |
| `update check` | Look for a newer version |
| global `--json`, `--jlink-library`, `--nrfutil` | [Command line](cli.md#global-options) |

All commands: [Command line](cli.md).
