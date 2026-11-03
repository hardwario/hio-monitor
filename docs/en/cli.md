# Command line: `hwio`

[Documentation](index.md) › Command line

`hwio` is the command-line tool of HARDWARIO Monitor. It does what the desktop app does with a J-Link, without a
window, and more: the Bluetooth LE console, the MCP server for AI assistants, low-level target commands, and output
for scripts (`--json`). Its commands follow py-hardwario's `hardwario` command, so most scripts keep working with the
command name changed ([Migrating from py-hardwario](cli-migration.md)).

`hwio` and the app share the settings, the command history, the console files and the J-Link library. The output
is always in English.

Where `hwio` is after installation: [Installing](install.md) (Windows: on the `PATH`; .deb: `/usr/bin/hwio`;
AppImage: `hwio-monitor-….AppImage hwio`; macOS: inside the app bundle, see **Settings → About → Command line**).

## Everyday use

```sh
hwio doctor                                  # check the installation
hwio jlink list                              # the connected J-Link probes
hwio chester app console                     # the console of the application MCU
hwio chester app command "info show"         # one shell command, print the answer
hwio chester app info                        # firmware name and version, serial number, ...
hwio chester app flash firmware.hex          # flash the application firmware
hwio chester lte flash                       # flash the HARDWARIO LTE firmware
hwio ble scan                                # CHESTER devices nearby
hwio ble console -a F1:22:33:44:55:66        # the console over Bluetooth
hwio --json chester app pib read | jq .data  # for scripts
```

`--help` after any command lists its options.

## Global options

These work with every command, before or after it.

| Option | Meaning |
|---|---|
| `-h`, `--help` | Show the help and exit |
| `--version` | Show the version and exit (`--version --json` also gives the Qt version, compiler and package type) |
| `--json` | Machine-readable output: one JSON object per line on stdout ([JSON output](#json-output)) |
| `--log-level LEVEL` | Show log messages on stderr from this level up: `debug`, `info`, `success`, `warning`, `error`, `critical`. The log file always gets everything |
| `--jlink-library PATH` | The SEGGER J-Link library to use. Default: `$HWIO_JLINK_LIBRARY`, the setting of the app, or the installed J-Link Software ([Installing](install.md#segger-j-link-software)) |
| `--nrfutil PATH` | nRF Util for the modem firmware. Default: `$HWIO_NRFUTIL`, the setting of the app, the `PATH`, or `~/.nrfutil/bin` |

### Choosing the probe

Commands that use a J-Link take these options (on the group, such as `hwio chester app -n …`, or on the command):

| Option | Meaning |
|---|---|
| `-n`, `--jlink-sn SERIAL_NUMBER` | The J-Link serial number. Default: the first probe found |
| `--jlink-speed auto\|KHZ` | J-Link clock speed: `auto` (default) or 5 to 50000 kHz |

## J-Link probes

| Command | Does |
|---|---|
| `hwio jlink list` | Lists the connected probes: serial number, product, connection, firmware, and which program uses it |
| `hwio jlink info [-n SN]` | Shows one probe and whether its firmware is up to date |
| `hwio jlink update-firmware [-n SN] [--yes]` | Updates the probe's firmware to the one bundled with the J-Link library (asks first) |

## CHESTER: application MCU (`hwio chester app`)

### Console

```sh
hwio chester app console [--reset] [--latency MS] [--history-file PATH] [--console-file PATH]
                         [--auto-reconnect] [--no-suggestions] [--linger S]
```

Starts the console: the device's shell and its log, with the commands you type sent to the shell.

| Option | Meaning |
|---|---|
| `--reset` | Reset the application firmware before attaching |
| `--latency MS` | How often the console (RTT) is read, in ms (default 50) |
| `--history-file PATH` | Command history file (default: the history shared with the app) |
| `--console-file PATH` | Console file; `''` for none. Default: a new file per session in the [console folder](console.md#console-files) |
| `--auto-reconnect` | Keep attaching again after the link drops, instead of staying disconnected until <kbd>F4</kbd> |
| `--no-suggestions` | No hints from the command history for this run |
| `--linger S` | Without a terminal or with `--json`: keep collecting output for S seconds after the input ends (default 1) |
| `--coredump-file PATH` | Accepted for compatibility with py-hardwario; no effect |

**Interactive** (in a terminal): <kbd>Enter</kbd> sends the line, <kbd>↑</kbd>/<kbd>↓</kbd> walk the history,
<kbd>Ctrl</kbd>+<kbd>R</kbd> searches it, grey hints come from the history, <kbd>Tab</kbd> completes the device's
commands, <kbd>F4</kbd> reconnects, <kbd>Ctrl</kbd>+<kbd>C</kbd> clears the line (on an empty line it quits),
<kbd>Ctrl</kbd>+<kbd>D</kbd> or <kbd>Ctrl</kbd>+<kbd>Q</kbd> quits. Device output goes to stdout, status lines
(`-- connected --`, `Console file: …`) to stderr.

**Non-interactive** (stdin is a file or a pipe): each input line is sent as a command; at the end of the input the
console waits `--linger` seconds and quits.

```sh
echo "info show" | hwio chester app console
hwio --json chester app console < commands.txt > session.ndjson
```

### One command

```sh
hwio chester app command [--reset] [-t SECONDS] [--console-file PATH] COMMAND
```

Sends the command (several commands: one per line) and prints the answer, collected for `-t` seconds (default 1)
after the command was echoed. Writes a console file per run unless `--console-file ''`.

### Information

| Command | Does |
|---|---|
| `hwio chester app info` | Shows what the device reports with `info show`: firmware name and version, product, hardware, serial number, … |
| `hwio chester app pib read` | Reads the Product Information Block from the UICR |
| `hwio chester app uicr read [--format hex\|bin] [--force] FILE` | Saves the UICR customer area (128 bytes) to FILE, or to stdout with `-` |
| `hwio chester app catalog-info ID` | Shows the name, version, upload time and SHA-256 of a catalog firmware |
| `hwio chester app check` | Checks the connection step by step: probe, power, connection, MCU type, console. Never writes to the device |

### Flashing

```sh
hwio chester app flash [--halt] [--allow-uicr] [--no-cache] [HEX_FILE_OR_ID]
```

Flashes a HEX file, or a 32-digit firmware ID from the catalog. Without an argument it looks for the build output in
`build/` or `build/zephyr/` of the current folder (`merged.hex`, `zephyr.hex`). The UICR and the Product Information
Block are kept ([Flashing](flashing.md#uicr-and-the-product-information-block)).

| Option | Meaning |
|---|---|
| `--halt` | Leave the CPU halted after flashing |
| `--allow-uicr` | Also program the image's UICR records (the PIB is kept) |
| `--no-cache` | Download a catalog firmware even when it is in the cache |

| Command | Does | Asks first |
|---|---|---|
| `hwio chester app erase` | Erases the application firmware; keeps the UICR and the PIB | no |
| `hwio chester app erase --all [--yes]` | Erases all flash and the UICR; the PIB is lost | yes |
| `hwio chester app reset [--halt]` | Resets the MCU (`--halt`: and keeps the CPU halted) | no |
| `hwio chester app recover [--yes]` | Removes read-back protection by erasing everything ([Recover](flashing.md#recover)) | yes |

### Low-level target commands

| Command | Does |
|---|---|
| `status` | Whether the CPU is halted, its core ID and the J-Link device |
| `halt`, `go` | Halt the CPU or let it run, without a reset |
| `regs` | The CPU registers (the CPU must be halted) |
| `zones` | The memory zones the J-Link library reports |
| `mem read [-l LEN] [-w 8\|16\|32] [-o FILE] ADDR` | Print memory as a hexdump (default 64 bytes, at most 16 MiB), or save the raw bytes to a new FILE |
| `mem write [-i FILE] [-w 8\|16\|32] [--yes] ADDR [HEX]` | Write bytes (for example `0102aabb`) to RAM or peripheral registers, at most 64 KiB (asks first) |
| `flash-write [-i FILE] [--allow-uicr] [--yes] ADDR [HEX]` | Program bytes into flash, at most 1 MiB; the rest of each flash page is kept (asks first). The PIB stays protected |

`-l` is `--length`, `-w` `--width` (bits), `-o` `--output`, `-i` `--input`. Addresses and lengths are decimal or `0x` hex. Writing memory can make the device run any code, which can also
erase its flash and the PIB.

## CHESTER: LTE MCU (`hwio chester lte`)

| Command | Does | Asks first |
|---|---|---|
| `hwio chester lte flash [--allow-uicr] [--no-cache] [FILE]` | Flashes the HARDWARIO LTE firmware (downloaded and verified) when no FILE is given, or a CHESTER LTE bundle `.zip`, a Nordic modem firmware `.zip` or an application `.hex` | no |
| `hwio chester lte erase [--yes]` | Erases all flash and the UICR of the LTE MCU; the PIB is lost | yes |
| `hwio chester lte reset` | Resets the LTE MCU and lets it run | no |
| `hwio chester lte recover [--yes]` | Erases everything, then programs the image that keeps the MCU debuggable | yes |
| `hwio chester lte check` | Checks the connection step by step: probe, power, connection, MCU type | no |

`status`, `halt`, `go`, `regs`, `zones`, `mem read`, `mem write` and `flash-write` work as for the application MCU.
The LTE MCU has no console. Modem firmware needs nRF Util.

## Bluetooth LE (`hwio ble`)

| Command | Does |
|---|---|
| `hwio ble scan [--timeout S] [--all] [--profile ID]` | Lists CHESTER devices nearby (5 s; `--all`: every device) |
| `hwio ble console (-a ID \| --name N) [--auto-reconnect] [--history-file PATH] [--console-file PATH]` | The console over Bluetooth |
| `hwio ble command (-a ID \| --name N) [-t SECONDS] [--console-file PATH] COMMAND` | One command; ends at the prompt or after `-t` seconds (default 3) |
| `hwio ble pair (-a ID \| --name N)` | Asks the system to pair; it asks for the passkey (Linux, Windows) |

`-a`/`--address ID` is the Bluetooth address (on macOS the device UUID shown by `hwio ble scan`); `--name N` is a part
of the advertised name, which exactly one device found by a 5-second scan must match. See [Bluetooth LE](bluetooth.md).

## Other commands

| Command | Does |
|---|---|
| `hwio doctor` | Checks the J-Link Software, nRF Util, USB access, programs holding a probe, the probes and their firmware, the folders and Bluetooth |
| `hwio diag report [-o\|--output FILE]` | Writes a diagnostic report for HARDWARIO support ([Privacy](privacy.md#diagnostic-report)) |
| `hwio console history clear [--yes]` | Deletes the saved command history of all devices (app and command line) |
| `hwio update check [--prerelease]` | Asks GitHub for the newest release; nothing is downloaded or installed |
| `hwio mcp [options]` | The MCP server for AI assistants ([MCP server](mcp.md)) |

`hwio doctor` prints one line per check:

```text
✓ J-Link Software              8.10 · /opt/SEGGER/JLink_V810
✓ nRF Util                     8.2.1, device 2.23.0
! USB access to J-Link         Accessible to every account
✓ Other programs using J-Link  None found
✓ J-Link probes                1 found: <serial>
✓ J-Link firmware              Not known
✓ Settings and log folders     Writable
✓ Bluetooth                    Adapter ready (<adapter>, <address>)

1 warning. Run `hwio diag report` to create a report for HARDWARIO support.
```

`!` is a warning, `✗` a failure; each comes with what to do. With `--json` each check has an `id` (`jlink-software`,
`nrfutil`, `usb-access`, `probe-holders`, `probes`, `probe-firmware`, `app-dirs`, `bluetooth`), and the exit code is
that of the first failed check.

## Confirmations

`erase --all`, `lte erase`, `recover`, `mem write`, `flash-write`, `jlink update-firmware` and
`console history clear` ask `[y/N]` in a terminal. Without a terminal, or with `--json`, they do not ask: add
`--yes`, otherwise they fail with [usage.confirmation_required](troubleshooting.md#usage.confirmation_required).
`flash`, `erase` without `--all`, and `reset` never ask.

<kbd>Ctrl</kbd>+<kbd>C</kbd> cancels a running command cleanly (exit code 130); the probe is released.

## JSON output

With `--json`, `hwio` prints one JSON object per line on stdout, an *envelope*:

```json
{"data":{...},"ok":true,"schema":1}
{"error":{"category":"probe","cause":"...","code":"probe.not_found","detail":"...","message":"...","suggestion":"...","wizard":null},"ok":false,"schema":1}
```

- `schema` changes only with incompatible changes.
- `error.code` is the stable error id ([Troubleshooting](troubleshooting.md)); `category` gives the exit code.
- Logs never go to stdout.

Commands that run for a while print several envelopes (NDJSON):

- **Console** (`chester app console`, `ble console`): `{"type":"line","seq","time","channel":"shell"|"log","dir":"out"|"in"|"marker","level","text"}`,
  `{"type":"connection","state":"connecting"|"probeOpen"|"connected"|"disconnected"|"stopped","error","transport"}`,
  `{"type":"dropped","count"}`, `{"type":"warning","error"}`, and at the end `{"type":"consoleFile","path"}` and a
  `stopped` connection event.
- **Flash, erase, recover**: `{"type":"progress","step","steps","phase","done","total"}`, then
  `{"type":"result",…}`.

```sh
hwio --json chester app command version | jq -r '.data.output[]'
hwio --json chester app console < cmds.txt | jq -c 'select(.data.type=="line" and .data.channel=="log")'
```

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | Internal error (a bug: please report it with the diagnostic report) |
| 2 | Invalid arguments or options; a confirmation is needed (`--yes`); `ble pair` on macOS |
| 3 | A file could not be read or written; settings |
| 4 | J-Link Software or nRF Util missing or too old; Bluetooth adapter missing, off or not allowed |
| 5 | J-Link probe not found, busy or with a firmware problem; Bluetooth device used by another program |
| 6 | The device does not respond, has no power or no console, is protected; Bluetooth connection problems |
| 7 | Invalid firmware image, verification failed, unknown catalog ID |
| 8 | Download or catalog failure |
| 9 | Timeout |
| 130 | Cancelled (<kbd>Ctrl</kbd>+<kbd>C</kbd>) |

The codes are part of the public interface and do not change between versions.

## Environment variables

| Variable | Meaning |
|---|---|
| `HWIO_JLINK_LIBRARY` | The J-Link library (below `--jlink-library`, above the setting) |
| `HWIO_NRFUTIL` | nRF Util (below `--nrfutil`, above the setting) |
| `HWIO_MCP_TOKEN` | The bearer token of `hwio mcp --http` ([MCP server](mcp.md#security)) |
| `HWIO_NO_CRASH_HANDLER=1` | No crash reports |
| `NO_COLOR` | No colours in the output |
