# Installing HARDWARIO Monitor

[Documentation](index.md) › Installing

HARDWARIO Monitor is one package per operating system. Each package holds the desktop app (`hwio-monitor`) and the
command-line tool (`hwio`). You also need **SEGGER J-Link Software** for the J-Link probe and, only for the LTE
modem firmware, **Nordic nRF Util**. Neither is included in the packages; HARDWARIO Monitor uses them from your
installation.

## Download

Get the latest release from the [releases page][releases].

| System | File |
|---|---|
| Windows 10 22H2 or newer, 64-bit | `hwio-monitor-<version>-windows-x64.msi` |
| macOS 13.3 or newer, Apple silicon and Intel | `hwio-monitor-<version>-macos-universal.dmg` |
| Ubuntu 24.04 or newer, Debian 13 or newer | `hardwario-monitor_<version>_amd64.deb` |
| Ubuntu 22.04 or newer and other Linux distributions with glibc 2.35 or newer, x86-64 | `hwio-monitor-<version>-x86_64.AppImage` |

Every release also has a `SHA256SUMS` file. To check a download:

```sh
sha256sum -c SHA256SUMS --ignore-missing
```

## Windows

1. Run the MSI. It installs for all users into `C:\Program Files\HARDWARIO\Monitor` and asks for administrator
   rights.
2. In the feature list:
   - **Add hwio to PATH** (on): the `hwio` command works in new terminals.
   - **Desktop shortcut** (off): a shortcut on the desktop.
3. Start **HARDWARIO Monitor** from the Start menu.

The installer is signed by HARDWARIO a.s. While a new release is still unknown to Microsoft SmartScreen, Windows may
warn before it runs: choose **More info → Run anyway**.

Installing a newer version replaces the older one; settings are kept. An older version cannot be installed over a
newer one ("A newer version of HARDWARIO Monitor is already installed."): uninstall first. Uninstall from
**Settings → Apps**.

Silent install, for example with the desktop shortcut:

```bat
msiexec /i hwio-monitor-<version>-windows-x64.msi /qn ADDDESKTOPSHORTCUT=1
```

## macOS

1. Open the DMG and drag **HARDWARIO Monitor** to **Applications**.
2. Start it from Applications. The app is signed by HARDWARIO and notarized by Apple, so it opens without a warning.

The `hwio` command is inside the app bundle and is not on the `PATH`. **Settings → About → Command line** shows its
path and a command that creates a link, which you can copy:

```sh
sudo ln -s '/Applications/HARDWARIO Monitor.app/Contents/MacOS/hwio' /usr/local/bin/hwio
```

**Bluetooth:** the first `hwio ble` command makes macOS ask for Bluetooth access. macOS gives the permission to the
app you run `hwio` from, usually Terminal. If you declined, allow it in **System Settings → Privacy & Security →
Bluetooth**.

To update, replace the app in Applications with the new one. To uninstall, move it to the Bin.

## Linux

### .deb (Ubuntu 24.04+, Debian 13+)

```sh
sudo apt install ./hardwario-monitor_<version>_amd64.deb
```

The package installs into `/opt/hardwario-monitor`, puts `hwio-monitor` and `hwio` into `/usr/bin`, adds the app to the
desktop menu, and installs the udev rule for J-Link probes (`/usr/lib/udev/rules.d/60-hardwario-jlink.rules`). It
depends on `bluez` for Bluetooth. Remove it with `sudo apt remove hardwario-monitor`.

### AppImage (Ubuntu 22.04+ and other distributions)

```sh
chmod +x hwio-monitor-<version>-x86_64.AppImage
./hwio-monitor-<version>-x86_64.AppImage
```

The AppImage holds both programs. It starts the command-line tool when `hwio` is the first argument, or when it is
started through a link named `hwio`:

```sh
./hwio-monitor-<version>-x86_64.AppImage hwio jlink list
ln -s "$PWD/hwio-monitor-<version>-x86_64.AppImage" ~/.local/bin/hwio
```

Without FUSE (some containers and minimal systems), add `--appimage-extract-and-run` or set
`APPIMAGE_EXTRACT_AND_RUN=1`. The AppImage does not update itself and does not install the udev rule: see the next
section.

### USB access to the J-Link

The .deb installs this rule. With the AppImage, install it once:

```sh
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1366", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/60-hardwario-jlink.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Then unplug the J-Link and plug it in again. The rule gives the probe to the user logged in at the computer, not to
every account. A user connected only over SSH has no such login and needs a rule of their own.

SEGGER's installer may add `/etc/udev/rules.d/99-jlink.rules` with `MODE="0666"`, which lets every account on the
computer use the probe. `hwio doctor` warns about it ([usb.world_accessible](troubleshooting.md#usb.world_accessible)).
Remove that rule, or its `MODE`, if you do not want that; a J-Link Software update may install it again.

### Bluetooth

Bluetooth uses BlueZ. If it is not running: `sudo systemctl start bluetooth`. Pairing needs a program that asks for
the passkey (a pairing agent), usually the desktop's Bluetooth settings. See [Bluetooth LE](bluetooth.md#pairing).

## SEGGER J-Link Software

Install **J-Link Software and Documentation Pack 8.00 or newer** from the SEGGER website. HARDWARIO Monitor finds it
in the default locations:

| System | Searched |
|---|---|
| Windows | the install path in the registry (`SOFTWARE\SEGGER\J-Link`), `C:\Program Files\SEGGER\JLink*` |
| macOS | `/Applications/SEGGER/JLink`, `/Applications/SEGGER/JLink_V*` |
| Linux | `/opt/SEGGER/JLink`, `/opt/SEGGER/JLink_V*` |

When several versions are installed, the newest is used. To use another copy, choose **Custom path** in
**Settings → J-Link → Library**, or for the command line pass `--jlink-library PATH` or set `HWIO_JLINK_LIBRARY`.
The order is: `--jlink-library`, then `HWIO_JLINK_LIBRARY`, then the setting, then the default locations. A path
that does not exist is an error; HARDWARIO Monitor then does not fall back to the default locations.

## Nordic nRF Util (LTE modem firmware only)

nRF Util updates the modem firmware of the LTE MCU. Everything else works without it. Install `nrfutil` from
Nordic Semiconductor, then its `device` command:

```sh
nrfutil install device
```

HARDWARIO Monitor looks for it in **Settings → nRF Util → Path** (or `--nrfutil PATH`, or `HWIO_NRFUTIL`), then on
the `PATH`, then in `~/.nrfutil/bin` (`%USERPROFILE%\.nrfutil\bin` on Windows) and `/usr/local/bin`. If it is too
old: `nrfutil self-upgrade` and `nrfutil upgrade device`.

## Check the installation

```sh
hwio doctor
```

`hwio doctor` checks the J-Link Software, nRF Util, USB access to the J-Link (Linux), other programs that hold a
probe, the connected probes and their firmware, the settings and log folders, and Bluetooth. Every problem comes with
what to do. In the app, the same checks are part of the diagnostic report (**Help → Create diagnostic report…**), and
**Settings → J-Link** and **Settings → nRF Util** show what was found.

Next: [Getting started](getting-started.md).

[releases]: https://github.com/hardwario/monitor/releases/latest
