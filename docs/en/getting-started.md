# Getting started

[Documentation](index.md) › Getting started

HARDWARIO Monitor shows the console and the log of HARDWARIO CHESTER devices and flashes their firmware, over a
SEGGER J-Link probe. Bluetooth LE and the MCP server for AI assistants are available in the command line
(`hwio`, see [Bluetooth LE](bluetooth.md) and [MCP server](mcp.md)).

Before you start, install the app and the SEGGER J-Link Software ([Installing](install.md)).

## The device

CHESTER has two MCUs, called **targets**:

| Target | MCU | What you can do |
|---|---|---|
| `app` | the application MCU (nRF52840) | console and log (RTT), flash, erase, reset, recover |
| `lte` | the LTE MCU (nRF9160 or nRF9151) | flash the LTE firmware and the modem firmware, erase, reset, recover; it has no console |

Connect the J-Link to the computer over USB and to the CHESTER's programming connector, and power the CHESTER with
batteries or through its USB-C power input.

## Connect

1. Start **HARDWARIO Monitor**. The first tab, **New device**, shows **Choose a device to connect** with the J-Link
   probes connected to this computer. The list refreshes every two seconds.
2. Pick the probe. A probe already open in another tab shows **In use (tab N)**; choosing it switches to that tab. A
   probe held by another program shows **In use by another program** and cannot be chosen.
3. Under **Device**, keep **CHESTER** and choose the target: **app** or **lte**.
4. Press **Connect**.

The device bar at the top of the tab shows the probe, the target and the state: **Connecting…**, **Connected ·
3.30 V** (with the target voltage), **Reconnecting…**, or **Disconnected — reason**. **Connect** and **Disconnect**
(<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>D</kbd>) are on the right, and <kbd>F4</kbd> reconnects.

If no probe is found, the page says **No J-Link probe found** and whether the J-Link Software was found. Check the
USB cable, and on Linux the udev rule ([Installing](install.md#usb-access-to-the-j-link)). `hwio doctor` checks all of
it.

### Connection options

**⋯** (**Advanced**) in the device bar holds the options of this probe. HARDWARIO Monitor remembers them per probe
serial number. They apply on the next connect.

| Option | Default | Meaning |
|---|---|---|
| **Automatic speed** | on | The J-Link picks the speed. Off: **J-Link speed (kHz)**, 5 to 50000 |
| **RTT latency (ms)** | 0 (the device's value, 50 ms for CHESTER) | How often the console is read |
| **Auto-reconnect** | on | Connect again by itself after the link drops |
| **Reset on connect** | off | Reset the device when connecting |
| **Save console to log file** | on | Write a [console file](console.md#console-files) |

Probes without their own options use the defaults in **Settings → J-Link**.

## Tabs

Each device is a tab; up to 8 can be open. <kbd>Ctrl</kbd>+<kbd>T</kbd> or **+** opens a new tab,
<kbd>Ctrl</kbd>+<kbd>W</kbd> closes it (middle-click on a tab does too), <kbd>Ctrl</kbd>+<kbd>Tab</kbd> and
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> switch. If an operation is running in the tab, closing it asks first.

The rail on the left switches the pages of the tab: **Console** (<kbd>Ctrl</kbd>+<kbd>1</kbd>) and **Flash**
(<kbd>Ctrl</kbd>+<kbd>2</kbd>). **Settings** (<kbd>Ctrl</kbd>+<kbd>,</kbd>) opens as a tab of its own.

At the next start the tabs come back, disconnected (**Settings → General → Reopen tabs at start**).

Only one window of the app runs at a time. Starting it again brings the open window to the front.

## Pages

- [Console](console.md): the device's shell and log, search, filter, history, batch files, console files.
- [Flashing](flashing.md): application and LTE firmware, erase, recover, reset.

## Settings

| Section | What is there |
|---|---|
| **General** | **Language** (System, English, Čeština), **Theme** (System, Light, Dark), **Reopen tabs at start** |
| **Console** | font size, timestamps, line wrap, command suggestions, command history, console files and their folder |
| **J-Link** | the J-Link library (**Automatic** or **Custom path**) and the default connection options |
| **nRF Util** | the nRF Util program (**Automatic** or **Custom path**), needed only for the LTE modem firmware |
| **About** | the version (with **Copy** for support), the [update check](privacy.md#update-check), the `hwio` command of this installation, licences, links |

Changes apply immediately.

## Help

**Help** at the bottom of the rail:

- **Keyboard shortcuts (F1)**: every shortcut of the app (also <kbd>Ctrl</kbd>+<kbd>/</kbd>).
- **Create diagnostic report…**: a zip file for HARDWARIO support ([Privacy](privacy.md#diagnostic-report)).
- **Documentation ↗**: the HARDWARIO Monitor page on docs.hardwario.com.

When something fails, the app shows what happened, the probable cause (**Probably:**) and what to do
(**What to do:**). **Details** shows the error code; [Troubleshooting](troubleshooting.md) lists every code.

## Command line

Everything the app does with a J-Link is also a command:

```sh
hwio jlink list                       # the connected probes
hwio chester app console              # the console of the application MCU
hwio chester app flash firmware.hex
hwio doctor                           # check the installation
```

See [Command line](cli.md).
