<h1 align="center">HARDWARIO Monitor</h1>

<p align="center">
  The console, log and flashing tool for HARDWARIO CHESTER devices,<br>
  as a desktop app and a command-line tool.
</p>

<p align="center">
  <a href="#download"><b>Download</b></a> ·
  <a href="docs/en/index.md">Documentation</a> ·
  <a href="docs/en/cli.md">Command line</a> ·
  <a href="docs/en/mcp.md">MCP server</a> ·
  <a href="docs/README.md">Čeština · Deutsch · Slovenčina · Polski</a>
</p>

## What it does

HARDWARIO Monitor connects to a CHESTER over a SEGGER J-Link probe and:

- shows the device's command shell and its log side by side, with search, filters, command history, completion of
  the device's commands and batch files;
- writes every session to a console file;
- flashes the application firmware from a `.hex` file or from the HARDWARIO firmware catalog, and the LTE and modem
  firmware;
- erases, resets and recovers the application and LTE MCUs, and keeps the device's Product Information Block safe;
- explains every error: what happened, why, and what to do.

It runs on Windows, macOS and Linux. `hwio`, its command-line sibling, does the same in a terminal and in scripts,
and adds the console over **Bluetooth LE**, an **MCP server** for AI assistants, low-level target commands and JSON
output. `hwio` keeps the commands of py-hardwario's `hardwario`, so existing scripts keep working.

HARDWARIO Monitor 2 is a complete rewrite of the earlier HARDWARIO Monitor.

## Features

### Console

The shell of the device on one side, its log on the other. Type a command with suggestions from the device's own
command list and from your history, search or filter the log by text, regular expression or level, pause the view,
copy lines, save a view, or run a batch file of commands. Each device is a tab, up to eight at a time, and the
console reconnects by itself after a reset or a power cut. [More](docs/en/console.md)

### Flashing

Pick **Application firmware** or **LTE firmware**, drop a file or type a catalog ID, and press **Flash**. The
recommended HARDWARIO LTE firmware is downloaded and verified for you. The UICR is written only when you allow it,
and the Product Information Block (serial number, claim token) is never overwritten by a flash. Erase, recover and
reset are on the same page. [More](docs/en/flashing.md)

### Command line

```sh
hwio doctor                                  # check the installation
hwio jlink list                              # the connected J-Link probes
hwio chester app console                     # the console of the application MCU
hwio chester app command "info show"         # one command, print the answer
hwio chester app flash firmware.hex          # or a 32-digit catalog ID
hwio chester lte flash                       # the HARDWARIO LTE firmware
hwio ble console -a F1:22:33:44:55:66        # the console over Bluetooth LE
hwio --json chester app pib read | jq .data  # JSON for scripts
```

[All commands](docs/en/cli.md) · [Migrating from py-hardwario](docs/en/cli-migration.md)

### MCP server

`hwio mcp` lets AI assistants (Claude Code, Claude Desktop, Cursor, VS Code) connect to a CHESTER, send commands,
read the log and wait for states, and, only when you allow it, flash and erase.

```sh
claude mcp add --transport stdio hwio -- hwio mcp --allow flash
```

[MCP server](docs/en/mcp.md)

### And also

- English and Czech user interface; light and dark theme
- Diagnostic report for HARDWARIO support, created on your computer, with your user name and secrets removed
- A notice when a newer version is out (checked on GitHub once a day; nothing about you is sent)
- No telemetry ([Privacy](docs/en/privacy.md))

## Download

Get the [latest release][releases]:

| System | File |
|---|---|
| Windows 10 22H2+ / 11, 64-bit | `hwio-monitor-<version>-windows-x64.msi` |
| macOS 13.3+, Apple silicon and Intel | `hwio-monitor-<version>-macos-universal.dmg` |
| Ubuntu 24.04+, Debian 13+ | `hardwario-monitor_<version>_amd64.deb` |
| Ubuntu 22.04+ and other Linux with glibc 2.35+, x86-64 | `hwio-monitor-<version>-x86_64.AppImage` |

Every package holds the app and `hwio`. Install **SEGGER J-Link Software 8.00 or newer** separately; for the LTE modem
firmware also **Nordic nRF Util**. Neither is included.

### First run

<details>
<summary><b>Windows</b></summary>

Run the MSI. It installs into `C:\Program Files\HARDWARIO\Monitor` and puts `hwio` on the `PATH` (feature **Add hwio
to PATH**). The installer is signed by HARDWARIO a.s.; while a new release is still unknown to SmartScreen, choose
**More info → Run anyway**.

</details>

<details>
<summary><b>macOS</b></summary>

Open the DMG and drag **HARDWARIO Monitor** to Applications. The app is signed and notarized. `hwio` is inside the app;
**Settings → About → Command line** shows the command that links it to `/usr/local/bin/hwio`. The first `hwio ble`
command asks for Bluetooth access for your terminal.

</details>

<details>
<summary><b>Linux</b></summary>

The .deb installs the app, `hwio` in `/usr/bin` and the udev rule for J-Link probes:

```sh
sudo apt install ./hardwario-monitor_<version>_amd64.deb
```

With the AppImage, install the udev rule yourself, then unplug and plug in the J-Link:

```sh
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1366", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/60-hardwario-jlink.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

The AppImage starts `hwio` when `hwio` is its first argument: `./hwio-monitor-<version>-x86_64.AppImage hwio doctor`.

</details>

Then run `hwio doctor`, or start the app and connect a CHESTER: [Getting started](docs/en/getting-started.md).
Details for each system: [Installing](docs/en/install.md).

### Updates

The app tells you when a newer version is out and opens its download page; it does not update itself. Install the
new version over the old one; settings are kept.

## Documentation

| | |
|---|---|
| User guide (English, Čeština, Deutsch, Slovenčina, Polski) | [Installing](docs/en/install.md), [Getting started](docs/en/getting-started.md), [Console](docs/en/console.md), [Flashing](docs/en/flashing.md), [Bluetooth LE](docs/en/bluetooth.md), [Troubleshooting](docs/en/troubleshooting.md), [Privacy](docs/en/privacy.md); other languages in [docs/](docs/README.md) |
| Reference | [Command line](docs/en/cli.md), [Migrating from py-hardwario](docs/en/cli-migration.md), [MCP server](docs/en/mcp.md) |
| Development | [CLAUDE.md](CLAUDE.md) (rules and commands), [plan/](plan/README.md) (architecture, feature plans, decisions), [packaging](packaging/README.md), [release checklist](packaging/RELEASE.md) |

## Building from source

Requirements: clang 21 (clang-cl on Windows), CMake with presets, Ninja, Qt 6.8 LTS (`QT_ROOT_DIR`), and SEGGER
J-Link Software to try it on hardware. Other dependencies are fetched by CMake (CPM) at pinned versions.

```sh
git lfs install                                  # once per clone
cmake --preset linux-debug                       # also macos-*, windows-*
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure
build/linux-debug/app/gui/hwio-monitor           # the app
build/linux-debug/app/cli/hwio --help            # the command line
```

Without hardware, tests and the CLI can run against a fake J-Link library and a JSON scenario
(`tests/fake_jlink/`), and the Bluetooth commands against a fake Bluetooth scenario (`tests/ble_scenarios/`); see
[CLAUDE.md](CLAUDE.md#commands).

<details>
<summary><b>Checks</b></summary>

```sh
cmake --build --preset linux-tidy                                   # clang-tidy, zero findings
cmake --build --preset linux-debug --target all_qmllint            # QML lint
cmake --build --preset linux-debug --target check_translations     # translations up to date
git ls-files -z -- '*.h' '*.cpp' | xargs -0 clang-format-21 --dry-run --Werror
tools/check-lfs.sh                                                  # binary files only through Git LFS
```

ASan + UBSan (`linux-asan`) and TSan (`linux-tsan`) presets run the same tests.

</details>

Every change starts as a plan in [plan/features/](plan/features/) and is decided with the maintainers; read
[CLAUDE.md](CLAUDE.md) and [plan/README.md](plan/README.md) first. Releases are built, signed and drafted by GitHub
Actions from a version tag ([release checklist](packaging/RELEASE.md)).

## Repository layout

| Path | Contents |
|---|---|
| `libs/core` | errors, logging, settings, bounded buffers, shared helpers |
| `libs/hw` | adapters to the SEGGER J-Link library, nRF Util and Bluetooth LE |
| `libs/device` | device profiles (CHESTER), sessions, console, flashing workflows |
| `libs/firmware` | HEX and zip images, the firmware catalog and cache |
| `libs/diag` | `hwio doctor` checks and the diagnostic report |
| `libs/mcp`, `libs/mcp_tools` | the MCP server and its tools |
| `libs/update` | the update check |
| `app/gui` | the desktop app (Qt Quick) |
| `app/cli` | `hwio` |
| `i18n` | translations (Qt Linguist) |
| `packaging` | packages for Linux, macOS and Windows |
| `tests` | fake J-Link and nRF Util, Bluetooth scenarios, integration tests |
| `docs` | user documentation |
| `plan` | architecture, feature plans and decisions (ADRs) |

## License

MIT, © 2026 HARDWARIO a.s. See [LICENSE](LICENSE).

The HARDWARIO and CHESTER names and the HARDWARIO logo are trademarks of HARDWARIO a.s. HARDWARIO Monitor uses Qt
(LGPLv3, linked dynamically), CLI11, replxx, miniz, the Inter and JetBrains Mono fonts and Material Symbols; their
licences are installed with the app and listed in **Settings → About**. SEGGER J-Link Software and Nordic nRF Util are
not part of HARDWARIO Monitor and have their own licences.

[releases]: https://github.com/hardwario/monitor/releases/latest
