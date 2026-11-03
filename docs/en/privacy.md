# Privacy and stored data

[Documentation](index.md) › Privacy

HARDWARIO Monitor has no telemetry and no account. It connects to the internet only for the things below, and
everything it stores stays on your computer.

## Network connections

| What | When | Where to |
|---|---|---|
| [Update check](#update-check) | the desktop app, at most once a day (can be turned off); `hwio update check` | `api.github.com` |
| Catalog firmware | when you flash by firmware ID, or run `hwio chester app catalog-info` | `firmware.hardwario.com` |
| HARDWARIO LTE firmware | when you flash the LTE firmware without your own file | `docs.hardwario.com` |
| Documentation links | when you click one | your browser |

The MCP server (`hwio mcp --http`) listens only on this computer (127.0.0.1) and is not reachable from the network.
SEGGER J-Link Software and nRF Util are separate programs with their own behaviour.

## Update check

The desktop app asks GitHub for the list of HARDWARIO Monitor releases 10 seconds after it starts, at most once in
24 hours. The request carries no data about you or your devices, only the app version in the `User-Agent` header.
When a newer version is out, a banner says so (**HARDWARIO Monitor 2.1.0 is available (you have 2.0.0).**) with
**Download**, which opens the release page, and **Skip this version**. The app never downloads or installs anything
itself.

**Settings → About → Updates**:

- **Check for updates automatically** (on): turn the daily check off.
- **Include pre-releases (beta, release candidates)** (off).
- **Check now**.

The command line checks only when you ask: `hwio update check` (`--prerelease` includes pre-releases).

## Diagnostic report

When you need help from HARDWARIO support, create a diagnostic report: **Help → Create diagnostic report…** in the
app, or `hwio diag report` (`-o FILE`). It is a zip file with:

- a summary: the app version, the operating system, the J-Link Software and nRF Util found, the connected J-Link
  probes, the open tabs, and the results of the checks of `hwio doctor`;
- the recent application logs (of the app and of `hwio`);
- the settings file;
- the latest crash reports, if there are any.

Before it is written, your user name, home folder and computer name are replaced (`<user>`, `~`, `<host>`), and values
that look like passwords, tokens or keys are replaced by `<redacted>`. Probe serial numbers stay in it. The logs may
contain output of your device, so look through the report before you send it. Console files and the command history
are not part of the report.

The report is saved on your computer (by default `hwio-diagnostics-YYYYMMDD-HHMMSS.zip` in Documents, or in the
current folder for `hwio diag report`); it is never uploaded. Attach it to your support request yourself.

## Crash reports

If the app or `hwio` crashes, a crash report is written into the `crashes` folder of the application data (at most
5 are kept). Nothing is sent. At the next start the app says **HARDWARIO Monitor quit unexpectedly last time.** and
offers **Create diagnostic report**, which includes the crash report. `HWIO_NO_CRASH_HANDLER=1` turns crash reports
off.

## What is stored where

| Data | Windows | macOS | Linux |
|---|---|---|---|
| Settings (`Monitor.ini`) | `%APPDATA%\HARDWARIO\Monitor.ini` | `~/.config/HARDWARIO/Monitor.ini` | `~/.config/HARDWARIO/Monitor.ini` |
| Application data | `%LOCALAPPDATA%\HARDWARIO\Monitor` | `~/Library/Application Support/HARDWARIO/Monitor` | `~/.local/share/HARDWARIO/Monitor` |

The application data folder holds:

| Folder | Contents | Limit |
|---|---|---|
| `console` | [console files](console.md#console-files) and the command history (`history-<device>-<target>.txt`) | 50 files and 200 MB; 1000 commands per history |
| `logs` | application logs, one file per run | 10 files of the app, 20 of `hwio`, 50 MiB each kind |
| `firmware-cache` | downloaded catalog firmware and LTE firmware | 100 files, 500 MiB |
| `crashes` | crash reports | 5 files |

The settings file, the logs and the console files are shared by the app and `hwio`. Console files and the history
can be read only by your user (Linux, macOS). Commands that carry secrets are not saved in the history and are
written as `<redacted>` in console files ([Console](console.md#command-history)).

Uninstalling HARDWARIO Monitor does not delete these folders; delete them yourself if you want. To delete only the
command history: **Settings → Console → Clear history**, or `hwio console history clear`.
