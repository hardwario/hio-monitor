# Flashing

[Documentation](index.md) › Flashing

The **Flash** page (<kbd>Ctrl</kbd>+<kbd>2</kbd>) writes firmware into the CHESTER over the J-Link and erases,
recovers and resets its MCUs. It works in three steps, and the **Flash** button at the bottom starts it
(<kbd>Ctrl</kbd>+<kbd>Enter</kbd>). When the button is disabled, the text next to it says why, for example **Choose a
firmware file or enter a catalog ID**.

While a flash runs, the console of the tab is paused and resumes by itself afterwards.

## 1. What do you want to flash?

| Choice | MCU | Firmware |
|---|---|---|
| **Application firmware** | the application MCU | a `.hex` file or a firmware ID from the catalog |
| **LTE firmware** | the LTE MCU and its modem | the HARDWARIO LTE firmware, or your own `.hex` / `.zip` |

The card shows the firmware that is in the device now, when the device reports it.

On a CHESTER-M, each MCU has its own programming connector. If the tab's target does not match what you want to flash,
the page tells you to connect the J-Link to the other connector and switch the tab's target first.

## 2. Firmware

### Application firmware

- Drop a `.hex` file anywhere on the page, or press **Browse…** (<kbd>Ctrl</kbd>+<kbd>O</kbd>).
- Or type a **firmware ID from the catalog** (32 characters, `0`–`9` and `a`–`f`) and press **Download**. The page
  shows the firmware's name, version and upload date (or **Unknown firmware — it can still be flashed.**).
  **Find firmware IDs ↗** opens the list of CHESTER catalog applications on docs.hardwario.com.

Downloaded firmware is kept in a cache and is not downloaded again. **Recent** lists the last 10
files and IDs.

### LTE firmware

- **HARDWARIO LTE firmware 1.7.0 (recommended)**: the LTE application and the modem firmware HARDWARIO tested with
  CHESTER. It is downloaded from docs.hardwario.com and its checksum is verified.
- **Use your own file…**: an application `.hex`, a CHESTER LTE bundle `.zip` (modem firmware and LTE application), or
  a Nordic modem firmware `.zip`. HARDWARIO Monitor looks at what is inside the file, not at its name, and rejects a
  zip that is neither.

The modem firmware needs **nRF Util** ([Installing](install.md#nordic-nrf-util-lte-modem-firmware-only)). Updating the
modem takes 1–3 minutes, and the console is disconnected meanwhile.

If you choose a file of the other kind, the page offers to switch (**This is LTE firmware — switch to LTE
firmware?**).

## 3. Options

- **Halt the CPU after flashing**: the CPU does not start the new firmware until you reset it or let it run.
- **Allow writing UICR**: shown only when the image contains UICR records; see below.

### UICR and the Product Information Block

The UICR is a small flash area with the device's configuration. On CHESTER it also holds the **Product Information
Block** (PIB): the serial number, the hardware variant and revision, the claim token and the BLE passkey.

- Flashing never writes the UICR unless you allow it (**Allow writing UICR**, or `--allow-uicr` on the command line),
  and it never changes the PIB.
- **Erase application** keeps the UICR and the PIB.
- **Erase everything** and **Recover** erase the PIB too. HARDWARIO Monitor cannot write it back, so they ask first.

`hwio chester app pib read` shows the PIB.

## Progress and result

The progress card shows the step (**Connecting**, **Erasing**, **Programming**, **Verifying**, **Resetting** and, for
the modem, **Updating the modem**), the bytes and the time. **Cancel** stops it; a cancelled or interrupted flash
leaves the device without working firmware until you flash it again.

At the end the page says, for example, **Firmware flashed and verified in 12 s.**, with **Open console** and
**Flash again**.

## Maintenance

**Maintenance — application MCU** (or **LTE MCU**) at the bottom of the page is folded by default:

| Action | Does | Asks first |
|---|---|---|
| **Reset** | Resets the MCU | no |
| **Reset and halt** | Resets the MCU and keeps the CPU halted | no |
| **Erase application** | Erases the firmware; keeps the UICR and the PIB (application MCU only) | no |
| **Erase everything…** | Erases the flash, the UICR and the PIB | yes |
| **Recover…** | See below | yes |

### Recover

When the MCU is read-back protected (approtect), the J-Link cannot read or write it, and flashing fails with
[target.protected](troubleshooting.md#target.protected). **Recover…** removes the protection by erasing the MCU
completely, including the UICR and the PIB. On the LTE MCU it then programs a small image that keeps the MCU
debuggable. After a recover, flash the firmware again. Recover never runs by itself.

## Command line

```sh
hwio chester app flash firmware.hex
hwio chester app flash 0123456789abcdef0123456789abcdef   # a catalog ID
hwio chester app flash                                    # the build output in build/ or build/zephyr/
hwio chester lte flash                                    # the HARDWARIO LTE firmware
hwio chester lte flash mfw_nrf91x1_2.0.2.zip              # a modem firmware zip
hwio chester app erase --all
hwio chester app recover
```

See [Command line](cli.md#flashing).
