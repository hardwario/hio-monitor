# Troubleshooting

[Documentation](index.md) › Troubleshooting

This page helps you when something does not work. It starts with the most common problems and then lists every error code.

## How errors are shown

Every error has a short message, a probable cause and a suggestion.

- **Desktop app (`hwio-monitor`):** the error panel shows the message, **Probably:** (the cause) and **What to do:** (the suggestion), with a button for the next step, such as **Reconnect**, **Open Settings** or **Retry**. **Details** shows the error code (for example `probe.not_found`) and technical details; **Copy** copies them.
- **CLI (`hwio`):** the error is written to stderr:

  ```text
  Error: No J-Link probe found.
  Cause: The USB cable is disconnected or the J-Link is not powered.
  What to do: Check the USB cable between the J-Link and the computer.
  Details: <technical details>
  ```

  The command ends with the [exit code](cli.md#exit-codes) of the error category.
- **CLI with `--json`:** the output is one line with `"ok":false` and an `error` object. Its fields are `code` (the error code), `category`, `message`, `cause`, `suggestion` and `detail`. See [JSON output](cli.md#json-output).

Each error code below has an anchor equal to the code, so you can link to it directly, for example `troubleshooting.md#probe.not_found`.

**Before you ask for help:**

- Run `hwio doctor`. It checks the J-Link Software, nRF Util, USB access to the J-Link (Linux), other programs that use the J-Link, the connected probes and their firmware, the settings and log folders, and Bluetooth.
- Create a diagnostic report: in the desktop app choose **Help** › **Create diagnostic report…**, or run `hwio diag report`. The report is a zip file with a summary, recent logs and the settings. See [Diagnostic report](privacy.md#diagnostic-report) for what it contains.

## Common problems

### No J-Link found

- Check the USB cable between the J-Link and the computer. Try another USB port or cable.
- Run `hwio jlink list` to see the probes. If you gave a serial number (`-n <serial>`), check it.
- On Linux, check the [USB permissions](#usb-permissions-on-linux).

Error codes: [`probe.not_found`](#probe.not_found), [`probe.open_failed`](#probe.open_failed), [`probe.io_failed`](#probe.io_failed).

### J-Link Software not found or too old

HARDWARIO Monitor does not include the SEGGER J-Link Software. Install version 8.00 or newer; see [SEGGER J-Link Software](install.md#segger-j-link-software). If it is installed in an unusual place, set the library in **Settings** › **J-Link** › **Library**, or use `--jlink-library PATH` (or `$HWIO_JLINK_LIBRARY`) in the CLI. Run `hwio doctor` to check.

Error codes: [`dependency.jlink_not_found`](#dependency.jlink_not_found), [`dependency.jlink_load_failed`](#dependency.jlink_load_failed), [`dependency.jlink_too_old`](#dependency.jlink_too_old), [`dependency.jlink_function_missing`](#dependency.jlink_function_missing).

### USB permissions on Linux

On Linux your user account needs access to the J-Link over USB. The `.deb` package installs a udev rule for this; for other installations, install it yourself as described in [USB access to the J-Link](install.md#usb-access-to-the-j-link). Then unplug and reconnect the J-Link and run `hwio doctor`. Some error texts mention a setup wizard; it is not available yet, so follow the install page instead.

Error codes: [`usb.access_denied`](#usb.access_denied), [`usb.world_accessible`](#usb.world_accessible).

### The J-Link is used by another program

Only one program can use a J-Link at a time. Close the other HARDWARIO Monitor window, the other `hwio` command, or other debugging tools (for example the SEGGER tools). In the desktop app, a probe used by another program is marked **In use by another program** on the device list. `hwio doctor` lists programs that may hold the probe.

Error codes: [`probe.in_use_by_other_process`](#probe.in_use_by_other_process), [`probe.held_by_program`](#probe.held_by_program), [`probe.open_failed`](#probe.open_failed).

### The device has no power or does not respond

- CHESTER must have its own power: batteries or the USB-C power input.
- Check the flat cable between the J-Link and CHESTER, and that it is in the connector of the MCU you want (app or LTE).
- If the MCU is read-back protected, see [Flashing fails](#flashing-fails-or-the-mcu-is-protected).

Error codes: [`target.no_power`](#target.no_power), [`target.connect_failed`](#target.connect_failed), [`target.wrong_family`](#target.wrong_family).

### No console (RTT) found

The console needs firmware with an RTT console. Reset the device, or flash such firmware. A very low J-Link speed can also cause it: use **Automatic speed**. The LTE MCU of CHESTER has no console; use the app MCU. See [Console](console.md).

Error codes: [`target.rtt_not_found`](#target.rtt_not_found), [`target.rtt_buffer_not_found`](#target.rtt_buffer_not_found), [`target.rtt_write_timeout`](#target.rtt_write_timeout), [`target.not_connected`](#target.not_connected).

### Flashing fails or the MCU is protected

- Check the cables and the power, then flash again.
- If verification fails again and again, set a lower J-Link speed.
- If the MCU is read-back protected, recover it. Recover erases the whole MCU, including the UICR and the Product Information Block. See [Recover](flashing.md#recover).
- If the firmware also writes the UICR, allow it on purpose: **Allow writing UICR** on the **Flash** page, or `--allow-uicr` in the CLI. See [UICR and the Product Information Block](flashing.md#uicr-and-the-product-information-block).

Error codes: [`target.protected`](#target.protected), [`target.program_failed`](#target.program_failed), [`firmware.verify_failed`](#firmware.verify_failed), [`firmware.uicr_in_image`](#firmware.uicr_in_image), [`firmware.out_of_range`](#firmware.out_of_range), [`target.recover_failed`](#target.recover_failed).

### LTE modem firmware needs nRF Util

Updating the LTE modem firmware uses Nordic nRF Util with its `device` command. Nothing else needs it. Install it as described in [Nordic nRF Util](install.md#nordic-nrf-util-lte-modem-firmware-only), or set its path in **Settings** › **nRF Util**, or with `--nrfutil PATH` in the CLI.

Error codes: [`dependency.nrfutil_not_found`](#dependency.nrfutil_not_found), [`dependency.nrfutil_device_missing`](#dependency.nrfutil_device_missing), [`dependency.nrfutil_too_old`](#dependency.nrfutil_too_old), [`firmware.modem_update_failed`](#firmware.modem_update_failed).

### Bluetooth device not found or pairing fails

Bluetooth is available only in the CLI (`hwio ble ...`).

- Turn Bluetooth on and run `hwio ble scan`. Only devices whose name contains `CHESTER` are shown; `--all` shows every device.
- Bring the device closer and disconnect it from other phones or computers.
- If pairing fails, remove the device in the system Bluetooth settings and pair again. On Linux a pairing agent must run to ask for the passkey. See [Pairing](bluetooth.md#pairing).

Error codes: [`ble.adapter_off`](#ble.adapter_off), [`ble.device_not_found`](#ble.device_not_found), [`ble.connect_failed`](#ble.connect_failed), [`ble.not_paired`](#ble.not_paired), [`ble.pairing_failed`](#ble.pairing_failed), [`ble.no_pairing_agent`](#ble.no_pairing_agent).

### Settings are read-only

If you used a newer version of HARDWARIO Monitor and then start an older one, the older version opens the settings read-only and shows the banner "Settings were created by a newer version and are read-only." Changes are not saved. Update HARDWARIO Monitor to the newer version.

Error code: [`settings.newer_schema`](#settings.newer_schema).

## Error codes

The error codes are grouped by category. The category decides the [exit code](cli.md#exit-codes) of `hwio`:

| Category | Meaning | Exit code |
|---|---|---|
| [`dependency`](#category-dependency) | A required program or system service is missing: J-Link Software, nRF Util, Bluetooth | 4 |
| [`probe`](#category-probe) | The J-Link probe or its USB connection | 5 |
| [`target`](#category-target) | The device: power, cable, MCU, console, Bluetooth link | 6 |
| [`firmware`](#category-firmware) | The firmware file or its download | 7 |
| [`network`](#category-network) | Internet connection: firmware catalog, update check | 8 |
| [`io`](#category-io) | Files, folders and settings | 3 |
| [`usage`](#category-usage) | A wrong argument, option or input | 2 |
| [`timeout`](#category-timeout) | Something did not answer in time | 9 |
| [`cancelled`](#category-cancelled) | The operation was stopped | 130 |
| [`internal`](#category-internal) | An internal problem of HARDWARIO Monitor | 1 |

A successful command ends with exit code 0. The texts below are the default English texts. Many places show a more exact message, for example one that names the MCU or the file; such variants are described under each code. Values in angle brackets, such as `<serial>`, are replaced by real values.

<a id="category-dependency"></a>
### Dependency (exit code 4)

<a id="dependency.jlink_not_found"></a>
#### `dependency.jlink_not_found`

**SEGGER J-Link Software was not found.**

- **Probably:** It is not installed, or it is installed in a non-standard location.
- **What to do:** Install J-Link Software 8.00 or newer, or set its path with --jlink-library.

Appears when HARDWARIO Monitor cannot find the J-Link library, for example right after installation. In the desktop app, set the path in **Settings** › **J-Link** › **Library** (**Custom path**); in the CLI use `--jlink-library PATH` or `$HWIO_JLINK_LIBRARY`. See [SEGGER J-Link Software](install.md#segger-j-link-software).

<a id="dependency.jlink_load_failed"></a>
#### `dependency.jlink_load_failed`

**The J-Link library could not be loaded.**

- **Probably:** The installation is damaged, is for another processor architecture, or lacks a required function.
- **What to do:** Reinstall J-Link Software.

The library file was found but could not be used, for example a 32-bit or x86 library on an ARM computer. Check the path in **Settings** › **J-Link** › **Library**, or `--jlink-library`.

<a id="dependency.jlink_too_old"></a>
#### `dependency.jlink_too_old`

**The J-Link Software is too old.**

- **Probably:** HARDWARIO Monitor needs J-Link Software 8.00 or newer.
- **What to do:** Install the current J-Link Software.

The message may name the version, as in "J-Link Software `<version>` is too old." See [SEGGER J-Link Software](install.md#segger-j-link-software).

<a id="dependency.nrfutil_not_found"></a>
#### `dependency.nrfutil_not_found`

**nRF Util was not found.**

- **Probably:** It is not installed; it is needed only for the modem firmware.
- **What to do:** Install nRF Util and its device command, or set its path with --nrfutil.

nRF Util is needed only to update the LTE modem firmware. In the desktop app, set the path in **Settings** › **nRF Util** › **Path** (**Custom path**); in the CLI use `--nrfutil PATH`. See [Nordic nRF Util](install.md#nordic-nrf-util-lte-modem-firmware-only).

<a id="dependency.nrfutil_device_missing"></a>
#### `dependency.nrfutil_device_missing`

**The nRF Util device command is not installed.**

- **Probably:** nRF Util is installed without its device command.
- **What to do:** Run `nrfutil install device`.

See [Nordic nRF Util](install.md#nordic-nrf-util-lte-modem-firmware-only).

<a id="dependency.nrfutil_too_old"></a>
#### `dependency.nrfutil_too_old`

**nRF Util is too old.**

- **Probably:** HARDWARIO Monitor needs a newer nRF Util or device command.
- **What to do:** Run `nrfutil self-upgrade` and `nrfutil upgrade device`.

The message may name the version, as in "nRF Util `<version>` is too old."

<a id="ble.adapter_not_found"></a>
#### `ble.adapter_not_found`

**No Bluetooth adapter was found.**

- **Probably:** The computer has no Bluetooth or it is disabled in the system.
- **What to do:** Enable Bluetooth or connect a USB Bluetooth adapter.

Bluetooth is available only in the CLI (`hwio ble ...`). `hwio doctor` also reports it as a warning. See [Bluetooth](bluetooth.md).

<a id="ble.adapter_off"></a>
#### `ble.adapter_off`

**Bluetooth is turned off.**

- **Probably:** Bluetooth is turned off in the system settings or with a hardware switch.
- **What to do:** Turn Bluetooth on in the system settings.

Bluetooth is available only in the CLI (`hwio ble ...`).

<a id="ble.permission_denied"></a>
#### `ble.permission_denied`

**HARDWARIO Monitor is not allowed to use Bluetooth.**

- **Probably:** Bluetooth access was denied in the privacy settings.
- **What to do:** Allow Bluetooth for HARDWARIO Monitor (or your terminal) in System Settings → Privacy & Security → Bluetooth.

Happens on macOS when the Bluetooth permission prompt was declined. When you run `hwio` from a terminal, allow Bluetooth for the terminal app.

<a id="ble.stack_unavailable"></a>
#### `ble.stack_unavailable`

**The Bluetooth service of the system is not available.**

- **Probably:** The Bluetooth service of the system (BlueZ on Linux) is not running.
- **What to do:** Start the Bluetooth service: `sudo systemctl start bluetooth`.

On Linux, the BlueZ service must be running. It also appears for an unknown error of the system Bluetooth stack during a scan.

<a id="ble.no_pairing_agent"></a>
#### `ble.no_pairing_agent`

**No program is ready to ask for the Bluetooth passkey.**

- **Probably:** No pairing agent is running (on GNOME the agent may only be active in Settings → Bluetooth).
- **What to do:** Pair the device from the system Bluetooth settings, or with bluetoothctl: `agent KeyboardOnly`, `default-agent`, `pair <id>`.

Linux only: the pairing was cancelled at once because no program could show the passkey dialog. See [Pairing](bluetooth.md#pairing).

<a id="dependency.jlink_function_missing"></a>
#### `dependency.jlink_function_missing`

**The installed J-Link software does not provide a function this operation needs.**

- **Probably:** The J-Link library is older than, or different from, the versions HARDWARIO Monitor was tested with.
- **What to do:** Install the latest J-Link Software and Documentation Pack.

Some target commands (for example registers or 16-bit memory access) need functions that only newer J-Link libraries provide. See [SEGGER J-Link Software](install.md#segger-j-link-software).

<a id="category-probe"></a>
### Probe (exit code 5)

<a id="probe.not_found"></a>
#### `probe.not_found`

**No J-Link probe found.**

- **Probably:** The USB cable is disconnected or the J-Link is not powered.
- **What to do:** Check the USB cable between the J-Link and the computer.

Appears when no J-Link is connected, or when the serial number you chose (`-n <serial>`) is not in the list. Variants: "No J-Link probe with serial number `<serial>` found." and, for a J-Link that is connected over the network, "The J-Link `<serial>` is a network J-Link; network J-Links are not supported for sessions yet." Use a J-Link connected over USB.

<a id="probe.in_use_by_other_process"></a>
#### `probe.in_use_by_other_process`

**The J-Link is used by another program.**

- **Probably:** Another HARDWARIO Monitor window or hwio command is using it.
- **What to do:** Close the other program, or choose another J-Link.

HARDWARIO Monitor and `hwio` lock a J-Link while they use it. The message can name the other program, as in "The J-Link `<serial>` is used by another program (`<app>`, PID `<pid>`)." If the program is unknown, the suggestion says to delete the lock file `<lockfile>` when no other HARDWARIO program is running. After the desktop app released the J-Link, the cause can also be "Another program opened the J-Link after it was released."

<a id="probe.open_failed"></a>
#### `probe.open_failed`

**The J-Link probe could not be opened.**

- **Probably:** It is used by another program or session, or the USB permissions are missing (Linux udev rules).
- **What to do:** Close other programs using the J-Link and try again.

On Linux, missing USB permissions are a common cause; see [USB access to the J-Link](install.md#usb-access-to-the-j-link). The variant "The J-Link `<serial>` is already used by this application." means another tab or session of the same program has it open.

<a id="probe.firmware_outdated"></a>
#### `probe.firmware_outdated`

**The J-Link firmware is outdated.**

- **Probably:** The J-Link firmware differs from the firmware bundled with the J-Link Software.
- **What to do:** Run `hwio jlink update-firmware`.

This is a warning; the J-Link keeps working. `hwio doctor` reports it too. When the probe firmware is newer than the library, the message is "The J-Link firmware is newer than the J-Link library." In that case, update the J-Link Software instead.

<a id="probe.firmware_update_failed"></a>
#### `probe.firmware_update_failed`

**The J-Link firmware could not be updated.**

- **Probably:** The update failed or the probe still reports a firmware different from the library's.
- **What to do:** Disconnect and reconnect the J-Link, then run `hwio jlink update-firmware` again.

Comes from `hwio jlink update-firmware`.

<a id="probe.io_failed"></a>
#### `probe.io_failed`

**Communication with the J-Link failed.**

- **Probably:** The J-Link did not answer: a USB glitch, or the USB cable was disconnected.
- **What to do:** Check the USB cable between the J-Link and the computer; the console reconnects automatically if enabled.

Happens when the USB connection to the J-Link is lost while the console runs. Auto-reconnect is on by default in the desktop app (**Settings** › **J-Link**, or **Advanced** (⋯) in the device bar); in the CLI use `--auto-reconnect`.

<a id="usb.access_denied"></a>
#### `usb.access_denied`

**This computer does not allow access to the J-Link (USB permissions).**

- **Probably:** The udev rule for SEGGER devices is missing.
- **What to do:** Install the udev rule shown in the setup wizard, then reconnect the J-Link. The rule gives access only to the user logged in at this computer; users connected only over SSH need their own udev rule.

Linux only. The setup wizard mentioned in the text is not available yet: install the udev rule as described in [USB access to the J-Link](install.md#usb-access-to-the-j-link), then run `hwio doctor` to check.

<a id="probe.held_by_program"></a>
#### `probe.held_by_program`

**Another program is running and may be using the J-Link.**

- **Probably:** Another debugging or programming tool keeps the probe open.
- **What to do:** Close the other program and try again.

A warning from `hwio doctor` and the diagnostic report. It can name the program, as in "`<program>` (PID `<pid>`) is running and may be using the J-Link."

<a id="ble.in_use_by_other_process"></a>
#### `ble.in_use_by_other_process`

**The device is used by another program.**

- **Probably:** Another HARDWARIO Monitor window or `hwio` command is connected to it.
- **What to do:** Close the other program.

Bluetooth devices are locked like J-Links. The message can name the other program ("The device `<id>` is used by another program (`<app>`, PID `<pid>`).") or say "The device `<id>` is already used by this application." If the program is unknown, the suggestion says to delete the lock file `<lockfile>` when no other HARDWARIO program is running.

<a id="usb.world_accessible"></a>
#### `usb.world_accessible`

**Every user account on this computer can use the J-Link (USB permissions).**

- **Probably:** A udev rule, usually /etc/udev/rules.d/99-jlink.rules from the SEGGER J-Link Software, makes the probe accessible to all accounts.
- **What to do:** Remove that rule (or its MODE="0666"), install the rule shown in the setup wizard, then reconnect the J-Link. A J-Link Software update may install the rule again.

A Linux warning from `hwio doctor`. The setup wizard mentioned in the text is not available yet: install the rule as described in [USB access to the J-Link](install.md#usb-access-to-the-j-link).

<a id="category-target"></a>
### Target (exit code 6)

<a id="target.no_power"></a>
#### `target.no_power`

**The device has no power.**

- **Probably:** The device is not powered or the flat cable is disconnected.
- **What to do:** Power the device and check the flat cable.

The message can include the measured voltage, as in "The device has no power (`<mV>` mV)." CHESTER needs batteries or the USB-C power input.

<a id="target.connect_failed"></a>
#### `target.connect_failed`

**Could not connect to the device.**

- **Probably:** The flat cable is disconnected or in the wrong connector, or the MCU is in a low-power or protected state.
- **What to do:** Check the flat cable between the J-Link and the device.

The J-Link works but cannot reach the MCU. Variants name the MCU, for example "Could not connect to the device's `<device>` MCU." or "The type of the `<target>` MCU could not be read." A protected MCU may need [Recover](flashing.md#recover).

<a id="target.rtt_not_found"></a>
#### `target.rtt_not_found`

**The device's console (RTT) was not found.**

- **Probably:** The firmware has no RTT console, is still booting, or the CPU is halted.
- **What to do:** Reset the device or flash firmware with RTT console support.

The console could not find the RTT control block in time. A very low J-Link speed can cause it too: use **Automatic speed** (**Advanced** (⋯) in the device bar, or **Settings** › **J-Link**). The LTE MCU of CHESTER has no console. See [Console](console.md).

<a id="target.rtt_buffer_not_found"></a>
#### `target.rtt_buffer_not_found`

**An RTT buffer was not found.**

- **Probably:** The firmware is older or different.
- **What to do:** Use firmware with the CHESTER shell.

The message names the buffer, as in "RTT buffer "`<name>`" not found." When only the log buffer is missing it is a warning ("…; the device log is not shown."), and the console still works.

<a id="target.rtt_write_timeout"></a>
#### `target.rtt_write_timeout`

**The device is not reading commands.**

- **Probably:** The firmware is hung or its shell is disabled.
- **What to do:** Reset the device.

The device did not take a command you sent in time.

<a id="target.rtt_read_failed"></a>
#### `target.rtt_read_failed`

**Reading from the device failed.**

- **Probably:** A cable was disconnected or the device was reset.
- **What to do:** Check the cables; the console reconnects automatically if enabled.

Auto-reconnect is on by default in the desktop app (**Settings** › **J-Link**, or **Advanced** (⋯) in the device bar).

<a id="target.rtt_write_failed"></a>
#### `target.rtt_write_failed`

**Writing to the device failed.**

- **Probably:** A cable was disconnected or the device was reset.
- **What to do:** Check the cables; the console reconnects automatically if enabled.

Auto-reconnect is on by default in the desktop app (**Settings** › **J-Link**, or **Advanced** (⋯) in the device bar).

<a id="target.not_connected"></a>
#### `target.not_connected`

**The device is not connected.**

- **Probably:** The console is disconnected or still reconnecting.
- **What to do:** Reconnect the console (F4).

You sent a command while the console was disconnected. With auto-reconnect on, the suggestion is "Wait for the console to reconnect." In the CLI, start the console again. If it happens during a batch file, the message is "The batch stopped after `<done>` of `<total>` commands."

<a id="target.wrong_family"></a>
#### `target.wrong_family`

**The J-Link is connected to another MCU.**

- **Probably:** The J-Link is connected to the other connector of the device.
- **What to do:** Connect the J-Link to the connector of the MCU you want to program.

The flat cable is on the connector of the other MCU (for example the LTE connector when you flash the app MCU). The message can be "Expected the `<target>` MCU (`<part>`) but found `<found>`."

<a id="target.protected"></a>
#### `target.protected`

**The MCU is read-back protected.**

- **Probably:** Access port protection (APPROTECT) is enabled.
- **What to do:** Recover the MCU (this erases it), then flash it again.

Recover removes the protection but erases the MCU completely, including the Product Information Block. See [Recover](flashing.md#recover). For a named MCU the suggestion is "Run `hwio <profile> <target> recover` (erases the MCU)."; in the desktop app use **Recover…** under **Maintenance** on the **Flash** page.

<a id="target.program_failed"></a>
#### `target.program_failed`

**Programming the MCU failed.**

- **Probably:** The connection was lost, the power is low or the MCU is protected.
- **What to do:** Check the cables and power, then try again.

The variant cause "Programming was interrupted; the firmware may be partly written." means you must flash again before the device works.

<a id="target.erase_failed"></a>
#### `target.erase_failed`

**Erasing the MCU failed.**

- **Probably:** The connection was lost, the power is low or the MCU is protected.
- **What to do:** Check the cables and power, then try again.

Two rare variants concern the Product Information Block after erasing only the firmware: "could not be checked after the erase" and "Erasing the code flash … also changed its Product Information Block". In both cases do not erase again; follow the suggestion and contact HARDWARIO support. See [UICR and the Product Information Block](flashing.md#uicr-and-the-product-information-block).

<a id="target.recover_failed"></a>
#### `target.recover_failed`

**Recovering the MCU failed.**

- **Probably:** The device has no power or its control access port does not answer.
- **What to do:** Power-cycle the device and try again.

See [Recover](flashing.md#recover).

<a id="pib.invalid"></a>
#### `pib.invalid`

**The Product Information Block is missing or damaged.**

- **Probably:** It was never written, or it was erased by a full erase or a recover.
- **What to do:** Contact HARDWARIO support to restore it.

The Product Information Block holds the serial number and hardware version. The message can name the failed check in parentheses. See [UICR and the Product Information Block](flashing.md#uicr-and-the-product-information-block).

<a id="ble.device_not_found"></a>
#### `ble.device_not_found`

**The Bluetooth device was not found.**

- **Probably:** The device is out of range, turned off or its Bluetooth is disabled.
- **What to do:** Bring the device closer and make sure its Bluetooth is on.

From `hwio ble console` or `hwio ble command` with `--name` or `-a`. Run `hwio ble scan` to see the devices in range. See [Bluetooth](bluetooth.md).

<a id="ble.connect_failed"></a>
#### `ble.connect_failed`

**Could not connect to the device over Bluetooth.**

- **Probably:** The signal is weak or the device is busy with another connection.
- **What to do:** Move closer, disconnect other phones or computers from the device and try again.

A variant cause is "The device refused to start the console: it may still be connected (to this or another computer) or the pairing was not completed." Then wait a few seconds, or disconnect the device in the system Bluetooth settings. See [Pairing](bluetooth.md#pairing).

<a id="ble.service_not_found"></a>
#### `ble.service_not_found`

**The device does not offer the console service (NUS).**

- **Probably:** The firmware has no Bluetooth shell.
- **What to do:** Use firmware with the Bluetooth console enabled.

The device is not running CHESTER firmware with a Bluetooth console.

<a id="ble.not_paired"></a>
#### `ble.not_paired`

**The device is not paired with this computer.**

- **Probably:** Pairing was cancelled, failed or was removed on one side.
- **What to do:** Pair the device: enter its passkey when the system asks.

Run `hwio ble pair` (Linux and Windows); on macOS the system asks for the passkey when you connect. See [Pairing](bluetooth.md#pairing).

<a id="ble.pairing_failed"></a>
#### `ble.pairing_failed`

**Pairing with the device failed.**

- **Probably:** A wrong passkey, a timeout or a stale bond on one side.
- **What to do:** Remove the device in the system Bluetooth settings and pair again.

On Linux the cause can also be that no passkey dialog appeared because no pairing agent is running. See [Pairing](bluetooth.md#pairing).

<a id="ble.write_failed"></a>
#### `ble.write_failed`

**Sending to the device over Bluetooth failed.**

- **Probably:** The link was lost during the write.
- **What to do:** Reconnect.

The Bluetooth link was lost while a command was sent.

<a id="ble.write_timeout"></a>
#### `ble.write_timeout`

**The device is not reading commands.**

- **Probably:** The firmware is busy or the link is stalled.
- **What to do:** Reconnect; if it repeats, restart the device.

The device did not confirm a command sent over Bluetooth in time.

<a id="ble.disconnected"></a>
#### `ble.disconnected`

**The Bluetooth connection to the device was lost.**

- **Probably:** The device is out of range, was restarted or turned off.
- **What to do:** Bring the device closer; the console reconnects automatically if enabled.

The message can name the device, as in "The Bluetooth connection to `<name>` was lost."

<a id="target.not_halted"></a>
#### `target.not_halted`

**The CPU is running.**

- **Probably:** Registers can be read only while the core is halted.
- **What to do:** Halt the core first.

From `hwio chester app regs` (or `lte regs`) and the MCP server (`hwio mcp`). Halt the core with `hwio chester app halt` first.

<a id="target.address_out_of_range"></a>
#### `target.address_out_of_range`

**The address range is not allowed for this operation.**

- **Probably:** Memory writes are allowed only in the RAM and peripheral ranges of the target.
- **What to do:** Use an address inside the RAM or peripheral ranges of the target.

From `mem read`, `mem write` and the MCP server (`hwio mcp`). The cause can also say that the range runs past the end of the address space, or that it overlaps CPU system or debug registers or the flash controller registers, which are never written.

<a id="target.memory_access_failed"></a>
#### `target.memory_access_failed`

**Reading or writing target memory failed.**

- **Probably:** The address is not mapped, or the core is in a fault state.
- **What to do:** Check the address and try again after a reset.

From `mem read`, `mem write` and the MCP server (`hwio mcp`).

<a id="category-firmware"></a>
### Firmware (exit code 7)

<a id="firmware.hex_parse_error"></a>
#### `firmware.hex_parse_error`

**The firmware file is not a valid HEX file.**

- **Probably:** The file is damaged or it is not a HEX file.
- **What to do:** Use the .hex file produced by the build or download it again.

The message can name the line, as in "The firmware file is not a valid HEX file (line `<n>`)."

<a id="firmware.invalid_image"></a>
#### `firmware.invalid_image`

**The file is not a supported firmware file.**

- **Probably:** It is not a CHESTER LTE bundle, a modem firmware zip or a HEX file, or the zip exceeds the safety limits.
- **What to do:** Use a CHESTER .hex file or LTE bundle (.zip).

HARDWARIO Monitor checks the file content, not the extension. Variants: "The file is not a CHESTER LTE bundle, modem firmware or HEX file." and, for a download, "The downloaded file is not `<name>`."

<a id="firmware.out_of_range"></a>
#### `firmware.out_of_range`

**The firmware does not fit the MCU.**

- **Probably:** The image was built for another MCU.
- **What to do:** Check that the firmware is for this MCU.

The message can give the address, as in "The firmware does not fit the `<target>` MCU (address `<addr>`)." The variant "The firmware writes into the Product Information Block (address `<addr>`)." means the image would overwrite the Product Information Block, which is never allowed.

<a id="firmware.uicr_in_image"></a>
#### `firmware.uicr_in_image`

**The firmware also writes the UICR configuration area.**

- **Probably:** The build includes UICR settings.
- **What to do:** Re-run with --allow-uicr if this is intended; the Product Information Block is kept.

`--allow-uicr` is the CLI option; in the desktop app turn on **Allow writing UICR** under **Options** on the **Flash** page. See [UICR and the Product Information Block](flashing.md#uicr-and-the-product-information-block).

<a id="firmware.verify_failed"></a>
#### `firmware.verify_failed`

**Verification of the written firmware failed.**

- **Probably:** The connection is unstable, or the flash is protected or faulty.
- **What to do:** Flash again; if it repeats, lower the J-Link speed or check the cables.

The message can give the address, as in "Verification failed at address `<0xADDR>`." Set a lower speed in **Advanced** (⋯) in the device bar, in **Settings** › **J-Link**, or with `--jlink-speed`.

<a id="firmware.not_found_in_catalog"></a>
#### `firmware.not_found_in_catalog`

**The firmware was not found in the catalog.**

- **Probably:** The firmware ID is wrong or it was deleted.
- **What to do:** Check the firmware ID.

Appears when you flash by a 32-digit firmware ID. The message can name the ID, as in "Firmware `<id>` was not found in the catalog."

<a id="firmware.modem_update_failed"></a>
#### `firmware.modem_update_failed`

**The modem firmware update failed.**

- **Probably:** The modem firmware zip is wrong, the connection was lost, or nRF Util reported an error (see Details).
- **What to do:** Check the cable and power, then flash the modem firmware again.

The LTE modem firmware is written by nRF Util; its output is in **Details**. See [Nordic nRF Util](install.md#nordic-nrf-util-lte-modem-firmware-only).

<a id="firmware.checksum_mismatch"></a>
#### `firmware.checksum_mismatch`

**The downloaded firmware is damaged (checksum mismatch).**

- **Probably:** The file was changed or damaged during the download.
- **What to do:** Try again; if it repeats, report it to HARDWARIO.

The downloaded file did not match the checksum from the catalog, so it was not used. The desktop app may show "The downloaded file is damaged. Try again."

<a id="category-network"></a>
### Network (exit code 8)

<a id="network.download_failed"></a>
#### `network.download_failed`

**Downloading the firmware failed.**

- **Probably:** There is no internet connection, a proxy blocks it, or the server failed.
- **What to do:** Check the internet connection and try again.

The message can name the firmware, as in "Downloading firmware `<id>` failed." or "Downloading `<name>` failed." In the desktop app you can also choose your own file instead.

<a id="update.network"></a>
#### `update.network`

**Could not reach GitHub to check for updates.**

- **Probably:** There is no internet connection, or a firewall or proxy blocks GitHub.
- **What to do:** Check your connection or try again later.

From the update check (**Settings** › **About**, or `hwio update check`).

<a id="update.rate_limited"></a>
#### `update.rate_limited`

**GitHub temporarily refused the update check.**

- **Probably:** GitHub limits how often a network address may ask; other computers on the same network may have used up the limit.
- **What to do:** Try again in an hour.

From the update check. GitHub limits the number of requests per network address.

<a id="update.bad_response"></a>
#### `update.bad_response`

**The update information from GitHub could not be read.**

- **Probably:** GitHub sent an unexpected answer, or a proxy changed it.
- **What to do:** Try again later; if it persists, check the releases page: https://github.com/hardwario/hio-monitor/releases

From the update check.

<a id="update.too_large"></a>
#### `update.too_large`

**The update information from GitHub could not be read.**

- **Probably:** GitHub sent an unexpected answer, or a proxy changed it.
- **What to do:** Try again later; if it persists, check the releases page: https://github.com/hardwario/hio-monitor/releases

From the update check, when the answer from GitHub is larger than allowed. It uses the same texts as `update.bad_response`.

<a id="category-io"></a>
### Files and settings: io (exit code 3)

<a id="io.file_not_found"></a>
#### `io.file_not_found`

**The file was not found.**

- **Probably:** The path is wrong or the file was moved or deleted.
- **What to do:** Check the file path (see Details).

The path is in **Details**. For the MCP server (`hwio mcp`), the message can be "The `<what>` `<dir>` does not exist." for a folder option such as `--dump-dir`.

<a id="io.read_failed"></a>
#### `io.read_failed`

**The file could not be read.**

- **Probably:** You do not have permission to read it or the disk reported an error.
- **What to do:** Check the file path and its permissions (see Details).

For a console batch file in the desktop app, the message is "The batch file could not be read."

<a id="io.write_failed"></a>
#### `io.write_failed`

**The file could not be written.**

- **Probably:** The disk is full or read-only, or you do not have permission to write.
- **What to do:** Check the path, the free disk space and the permissions (see Details).

The path is in **Details**. Variants: "The file could not be saved." when saving the console in the desktop app, and "The console log file could not be written; logging to file is off for this session." For the MCP server (`hwio mcp`), the message can be "The `<what>` `<dir>` is not writable."

<a id="settings.newer_schema"></a>
#### `settings.newer_schema`

**Settings were written by a newer version and are opened read-only.**

- **Probably:** An older version of HARDWARIO Monitor is running now.
- **What to do:** Changes will not be saved; update HARDWARIO Monitor.

Happens after you used a newer version and then started an older one. The desktop app shows the banner "Settings were created by a newer version and are read-only." The settings file is not changed, so the newer version still has all its settings.

<a id="diag.report_write_failed"></a>
#### `diag.report_write_failed`

**The diagnostic report could not be saved.**

- **Probably:** The folder is not writable or the disk is full.
- **What to do:** Choose another folder.

From **Help** › **Create diagnostic report…** or `hwio diag report`. See [Diagnostic report](privacy.md#diagnostic-report).

<a id="mcp.port_in_use"></a>
#### `mcp.port_in_use`

**The MCP port is already in use.**

- **Probably:** Another HARDWARIO Monitor, `hwio mcp --http` or another MCP server (for example rttt) is listening on it.
- **What to do:** Stop the other server or choose another port with --port.

MCP server (`hwio mcp --http`). See [MCP](mcp.md).

<a id="mcp.listen_failed"></a>
#### `mcp.listen_failed`

**The MCP server could not start.**

- **Probably:** The operating system refused to open the listening socket on 127.0.0.1.
- **What to do:** Choose another port with --port.

MCP server (`hwio mcp --http`).

<a id="mcp.token_file"></a>
#### `mcp.token_file`

**The MCP token file cannot be read or created.**

- **Probably:** The file or its folder has the wrong permissions, or the file does not contain a valid token.
- **What to do:** Check the file permissions, or delete the file to create a new token.

MCP server (`hwio mcp --http`). See [Security](mcp.md#security).

<a id="io.file_exists"></a>
#### `io.file_exists`

**The output file already exists.**

- **Probably:** Existing files are never overwritten by this command.
- **What to do:** Choose a new file name or delete the file first.

From commands that write a file with `-o` (for example `uicr read` or `mem read`) and from memory dumps of the MCP server (`hwio mcp`). Add `--force` to replace the file.

<a id="cli.file_is_symlink"></a>
#### `cli.file_is_symlink`

**The output file is a symbolic link.**

- **Probably:** Symbolic links are never followed or replaced, even with --force.
- **What to do:** Choose another file name or remove the link first.

CLI only, with `-o FILE --force`.

<a id="category-usage"></a>
### Usage (exit code 2)

<a id="usage.invalid_argument"></a>
#### `usage.invalid_argument`

**An argument or option is invalid.**

- **Probably:** The request contains a value that is not supported.
- **What to do:** Check the arguments and try again.

The message usually says exactly which value is wrong, for example a bad option (the suggestion is then "Run `hwio --help`."), an unknown device profile or target, a J-Link speed or latency out of range, a bad address or length, a firmware ID that does not have 32 hexadecimal digits, or several Bluetooth devices matching `--name` (use `--address`). In the desktop app it also covers console batch files that are too large, contain a command that is too long, or contain no commands. See [CLI](cli.md).

<a id="usage.confirmation_required"></a>
#### `usage.confirmation_required`

**This operation needs confirmation.**

- **Probably:** The command runs non-interactively, so it cannot ask.
- **What to do:** Re-run with --yes.

CLI only. Commands that erase or write (flash, erase, recover, memory and UICR writes, `jlink update-firmware`, console batch) ask before they start. Without a terminal, or with `--json`, they cannot ask.

<a id="ble.pairing_not_supported"></a>
#### `ble.pairing_not_supported`

**This system pairs Bluetooth devices only when connecting.**

- **Probably:** macOS pairs a device automatically when it connects.
- **What to do:** Connect to the device; macOS asks for the passkey.

macOS only, from `hwio ble pair`. See [Pairing](bluetooth.md#pairing).

<a id="mcp.tool_not_allowed"></a>
#### `mcp.tool_not_allowed`

**This MCP tool is not allowed by the server.**

- **Probably:** Destructive tools must be allowed when the server starts.
- **What to do:** Start the server with --allow and the tool's category, or with --allow all.

MCP server (`hwio mcp`). The message can name the tool and the items to allow, as in "Start the server with --allow `<items>` (or --allow all)." See [Security](mcp.md#security).

<a id="mcp.device_ambiguous"></a>
#### `mcp.device_ambiguous`

**Several devices are connected and the tool does not know which one to use.**

- **Probably:** The device argument was left out while more than one device is open.
- **What to do:** Pass the device argument; list_devices shows the ids.

MCP server (`hwio mcp`). `list_devices` is an MCP tool.

<a id="mcp.device_not_open"></a>
#### `mcp.device_not_open`

**The device is not connected.**

- **Probably:** It was never connected, it was disconnected, or its tab was closed.
- **What to do:** Call connect first.

MCP server (`hwio mcp`). `connect` is an MCP tool.

<a id="mcp.capability_missing"></a>
#### `mcp.capability_missing`

**This tool needs a J-Link connection.**

- **Probably:** The device is connected over Bluetooth, which provides only the console.
- **What to do:** Connect the device with a J-Link probe.

MCP server (`hwio mcp`).

<a id="mcp.to_file_not_allowed"></a>
#### `mcp.to_file_not_allowed`

**The file name must be a plain name.**

- **Probably:** Memory dumps are written only into the server's dump folder.
- **What to do:** Use a plain file name such as flash.bin, or start the server with --dump-dir DIR.

MCP server (`hwio mcp`). The message can name the dump folder. See [Security](mcp.md#security).

<a id="category-timeout"></a>
### Timeout (exit code 9)

<a id="timeout"></a>
#### `timeout`

**The operation timed out.**

- **Probably:** The other side did not respond in time.
- **What to do:** Try it again.

A general timeout: for example the device did not connect in time ("The device did not connect in time."), flashing or erasing took too long, or nRF Util did not finish. `hwio chester app info` uses its own texts when the firmware does not answer `info show`.

<a id="diag.check_timeout"></a>
#### `diag.check_timeout`

**A check did not finish in time.**

- **Probably:** The system is slow or a tool does not answer.
- **What to do:** Run the checks again.

A warning from `hwio doctor` or the diagnostic report. The message can name the check, as in "The check "`<title>`" did not finish in time."

<a id="mcp.consent_timeout"></a>
#### `mcp.consent_timeout`

**Nobody answered the permission request.** Reserved for the MCP server of the desktop app, which is not available yet.

<a id="category-cancelled"></a>
### Cancelled (exit code 130)

<a id="cancelled"></a>
#### `cancelled`

**Cancelled by user.**

- **Probably:** The operation was stopped before it finished.
- **What to do:** Run the operation again if needed.

Appears when you press Ctrl+C or **Cancel**, answer No to a confirmation, or close the device. The variant "The output was closed." means the program reading the output of `hwio` stopped, for example `hwio ... --json | head`.

<a id="mcp.consent_denied"></a>
#### `mcp.consent_denied`

**The user did not allow this action.** Reserved for the MCP server of the desktop app, which is not available yet.

<a id="category-internal"></a>
### Internal (exit code 1)

<a id="internal.unexpected"></a>
#### `internal.unexpected`

**An unexpected internal error occurred.**

- **Probably:** This is a bug in HARDWARIO Monitor.
- **What to do:** Please report it with the log file (see Details).

Please create a [diagnostic report](privacy.md#diagnostic-report) and send it to HARDWARIO.

<a id="internal.queue_full"></a>
#### `internal.queue_full`

**An internal buffer is full.**

- **Probably:** This is a bug in HARDWARIO Monitor or the computer is overloaded.
- **What to do:** Please report it with the log file (see Details).

For example, the device sends console lines faster than they can be shown. Please create a [diagnostic report](privacy.md#diagnostic-report).

<a id="internal.session_limit"></a>
#### `internal.session_limit`

**Too many devices are open (max 8).**

- **Probably:** HARDWARIO Monitor works with at most 8 devices at the same time.
- **What to do:** Close a device first.

<a id="internal.busy"></a>
#### `internal.busy`

**Another operation is running on this device.**

- **Probably:** Only one operation can run at a time.
- **What to do:** Wait for it to finish or cancel it.

For example, you started a flash while a console batch file was running. If a batch file is interrupted this way, the message is "The batch stopped after `<done>` of `<total>` commands."

<a id="mcp.too_many_requests"></a>
#### `mcp.too_many_requests`

**Too many MCP requests are running.**

- **Probably:** The server runs at most 32 tool calls at the same time.
- **What to do:** Wait for running calls to finish and try again.

MCP server (`hwio mcp`).
