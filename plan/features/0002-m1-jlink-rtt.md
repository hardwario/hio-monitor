# 0002 — M1 J-Link & RTT: runtime J-Link library, probes, DeviceSession, RTT console, CLI

- **Status:** Draft
- **Milestone:** M1
- **Author:** Claude (with Matej Strnad), 2026-10-09
- **Related ADRs:** 0004, 0006, 0007, 0010, 0015, 0017, 0020, 0022–0026, 0027–0034, 0053, 0058, 0063
- **Depends on:** 0001 (M0 skeleton, incl. the translation infrastructure)

> **Amended by ADR-0053, ADR-0058 and ADR-0063 (M3 planning, applied in this text):** `DeviceSession::open()` is
> non-blocking and the CLI waits with `waitUntilConnected()`. Probes are locked across processes (`ProbeLock`,
> `probe.in_use_by_other_process`). Command-history saves are merged under a file lock. The `ConsoleLog` can be
> provided by the session's owner, and marker lines exist.

> **Pending amendment from M4 (ADR-0069, specified in [0007](0007-m4-wizard-framework-diagnostics.md) § Amendment;
> applied to this text when 0007 is approved — then this plan needs re-approval):** `SessionOptions::connectTarget`
> (probe-only sessions) and the `ProbeOpen` state; `DeviceSession::diagnose(targetId)` and `startConnection(attachRtt)`;
> on Linux an open failure checks USB access and reports `usb.access_denied` (ADR-0072);
> `probe.in_use_by_other_process` gets the Troubleshooting wizard.

> **Pending amendment from M5 (ADR-0079, ADR-0083, specified in [0011](0011-m5-ble-backend-cli.md); applied to this
> text when 0011 is approved — then this plan needs re-approval):** `DeviceSession` derives from the new abstract
> `ConsoleSession` (identity, capabilities, `consoleFilePath`, `promptSeen`); `waitUntilConnected` /
> `waitForFirstAttempt` and the CLI console take `ConsoleSession&`; `SessionRegistry` is keyed by `SessionKey =
> std::variant<ProbeKey, BleKey>` (J-Link and BLE share the 8-session limit).

## Context

The J-Link RTT console (interactive shell + device log) is the core feature of HARDWARIO Monitor. The old
app did it with defects that must not recur (plan/0002): the J-Link shared between flasher and RTT without an
owner, leaked threads and data races on DLL calls, an infinite loop when an RTT write returned 0, silent
failures (detach always "succeeded"), unbounded buffers, and log lines detected by string matching.

py-hardwario's `hardwario chester app console` / `command` use **rttt 1.9.2** (`PyLinkRTTConnector` over
pylink), which is the behavioural reference (ADR-0034). Its sources were read for this plan
(`rttt/connectors/pylink_rtt.py`, `mcp_server.py`, `file_log.py`, `event.py`) together with
py-hardwario `hardwario/chester/cli/app.py`, `hardwario/chester/connector.py` and `hardwario/device/__init__.py`.

M1 delivers the backend for this and its CLI. The GUI console page is M3, flashing M2, MCP M6.

## Goals / Non-goals

**Goals**

1. `libs/hw/jlink`: locate and load the JLinkARM library at runtime (ADR-0006), own declarations
   (ADR-0028), version check ≥ 8.00 (ADR-0029), one library copy per session (ADR-0030), probe enumeration,
   probe open/connect, hardware status, probe firmware status/update, RTT channel (ADR-0034).
2. `libs/device`: CHESTER device profile (ADR-0032), console line assembly, bounded console log store,
   command history, console log files, `DeviceSession` with its worker thread (ADR-0031) and a session registry.
3. CLI: `hio chester app console`, `hio chester app command`, `hio jlink list|info|update-firmware`,
   global `--jlink-library`.
4. A test-only fake J-Link library so that everything above is tested in CI without hardware;
   opt-in hardware-in-the-loop tests with a CHESTER.
5. Every user-visible text translatable (ADR-0027) and every error an `hio::Error` with cause and suggestion.

**Non-goals**

- Programming, erase, recover, PIB/UICR, MCU family check (M2). The session only exposes `suspend()/resume()`
  so M2 can flash through it.
- LTE (nRF91) console; the LTE target is declared in the profile but not used.
- GUI pages (M3), wizards (M4; errors already carry `WizardId`s), BLE (M5), MCP and the `--mcp*` console
  options (M6), coredump handling.

## User experience

No GUI changes in M1. The CLI console:

```
$ hio chester app console
Using J-Link 801234567 (J-Link EDU Mini, firmware 2024-10-01), library 8.10a
Connecting to NRF52840_xxAA at 2000 kHz…
-- connected (RTT: Terminal, Logger) --
[00:00:01.234,000] <inf> app: Build version: v3.1.0          ← colored by level, log channel
uart:~$ version                                               ← replxx prompt at the bottom
Firmware version: v3.1.0
-- disconnected: target has no power (VTref 0 mV) --
-- connected --
^D
TIP: After a J-Link connection, power-cycle the device; otherwise the CPU stays in debug mode with increased power consumption.
```

Keys: Enter sends the line; Up/Down history; Ctrl+R history search (replxx); **F4** reconnect;
**Ctrl+D**, **Ctrl+Q**, or **Ctrl+C on an empty line** quit; Ctrl+C on a non-empty line clears it.
Log levels: `<err>` red, `<wrn>` yellow, `<inf>` default, `<dbg>` dim; colors only when stdout is a TTY and
`NO_COLOR` is unset. Shell output is printed as-is (ANSI sequences stripped, see Design).

The CLI is English only (ADR-0027).

## CLI

| Command | Behaviour |
|---|---|
| `hio chester app console [--reset] [--latency MS=50] [--history-file P] [--console-file P] [-n/--jlink-sn SN] [--jlink-speed KHZ=2000] [--auto-reconnect] [--coredump-file P]` | Interactive console (ADR-0033). `--reset` resets the target (no halt) before attaching. Without `--auto-reconnect` a dropped link stays down until F4. `--coredump-file` is accepted for py-hardwario compatibility, has no effect and logs a warning. `--console-file ''` disables the console log file. Exit 0 on user quit; an attach error is an `hio::Error` with its exit code. |
| `hio chester app command CMD [--reset] [-t/--timeout S=1.0] [--console-file P] [-n SN] [--jlink-speed KHZ]` | py-hardwario semantics: attach, send each line of `CMD`, collect terminal output until the **absolute** timeout (counted from sending), detach. Human: output lines on stdout. `--json`: `data = {"command": "…", "output": ["…"], "log": ["…"]}`. |
| `hio jlink list` | Enumerate probes (separate short-lived library instance). Human: table SN / product / connection / firmware / in use (e.g. "hio-monitor, PID 1234", ADR-0058). `--json`: `data = {"library": {"path","version"}, "probes": [{"serialNumber","product","connection":"usb"\|"ip","firmware","inUseBy":{"app","pid"}\|null}]}`. No probe → empty list, exit 0. |
| `hio jlink info [-n SN]` | Library path + version, probe SN, product, firmware string, `firmwareStatus` (`current`/`outdated`/`newer`), target voltage (mV). |
| `hio jlink update-firmware [-n SN] [--yes]` | Update the probe firmware to the one bundled in the library (or synchronize a newer one). Asks for confirmation on a TTY; non-TTY requires `--yes` (else `usage.confirmation_required`). |
| global `--jlink-library PATH` | Library override; precedence: this option > env `HIO_JLINK_LIBRARY` > setting `jlink/libraryPath` > auto-discovery. |

Probe choice when `-n` is not given: the **first** probe from the library's list is used; its SN is printed
(stderr in human mode) and logged. SEGGER dialogs are always suppressed. With `-n`, an unknown SN →
`probe.not_found`. A probe locked by another HARDWARIO process (ADR-0058) → `probe.in_use_by_other_process`
(exit 5); without `-n` the first probe that is not locked is used.

**`--json` console stream** (NDJSON, ADR-0026; one envelope per line, `data` holds the event):

```json
{"schema":1,"ok":true,"data":{"type":"line","seq":17,"time":"2026-10-09T12:34:56.789Z","channel":"log","dir":"out","level":"inf","text":"[00:00:01.234,000] <inf> app: Build version: v3.1.0"}}
{"schema":1,"ok":true,"data":{"type":"connection","state":"disconnected","error":{"code":"target.no_power", "...": "..."}}}
{"schema":1,"ok":true,"data":{"type":"dropped","count":120}}
{"schema":1,"ok":true,"data":{"type":"warning","error":{"code":"probe.firmware_outdated", "...": "..."}}}
```

`channel` = `shell`|`log`, `dir` = `out`|`in` (in = echoed command), `level` = `dbg`|`inf`|`wrn`|`err`|`null`,
`state` = `connecting`|`connected`|`disconnected`|`stopped`. In `--json` mode stdin lines are sent as commands;
EOF ends the session; a fatal error ends with an error envelope and the matching exit code.

Exit codes per ADR-0024 (no new categories).

## Design

### Modules

```
libs/hw/jlink/   hio_hw_jlink  (QtCore)          include/hio/hw/jlink/*.h  src/*  tests/*
libs/device/     hio_device    (QtCore, hio_hw_jlink)  include/hio/device/*.h  src/profiles/*  tests/*
app/cli/src/     jlink_commands.cpp, chester_commands.cpp, console_ui.{h,cpp} (replxx)
tests/fake_jlink/   test-only shared library exporting the J-Link C symbols
tests/integration/  HIL tests (opt-in)
```

Both libraries are created with `hio_add_backend_library` (static, ADR-0022; Qt GUI modules forbidden, ADR-0004).

### `libs/hw/jlink`

- **`src/jlink_abi.h`** (private, ADR-0028) — typed pointers for (final list maintained here during
  implementation): `JLINKARM_Open`, `JLINKARM_Close`, `JLINKARM_IsOpen`, `JLINKARM_GetDLLVersion`,
  `JLINKARM_GetCompileDateTime`, `JLINKARM_EMU_GetList`, `JLINKARM_EMU_SelectByUSBSN`, `JLINKARM_ExecCommand`
  (`SetBatchMode = 1`, `DisableInfoWinFlashDL`, `HideDeviceSelection`, `SilentUpdateFW`, `Device = <name>`),
  `JLINKARM_TIF_Select`, `JLINKARM_SetSpeed`, `JLINKARM_Connect`, `JLINKARM_IsConnected`, `JLINKARM_GetHWStatus`,
  `JLINKARM_GetFirmwareString`, `JLINKARM_GetEmbeddedFWString`, `JLINKARM_UpdateFirmwareIfNewer`, `JLINKARM_GetSN`,
  `JLINKARM_Reset`, `JLINKARM_ResetNoHalt`, `JLINKARM_Halt`, `JLINKARM_Go`, `JLINKARM_SetResetDelay`,
  `JLINKARM_SetLogOutHandler`, `JLINKARM_SetWarnOutHandler`, `JLINKARM_SetErrorOutHandler`,
  `JLINK_RTTERMINAL_Control`, `JLINK_RTTERMINAL_Read`, `JLINK_RTTERMINAL_Write`; structs
  `EMU_CONNECT_INFO`, `HW_STATUS`, `RTTERMINAL_BUFDESC`, `RTTERMINAL_START` with `static_assert`ed sizes.
  Windows calling convention handled here.
- **C callbacks** (log/warn/error out handlers): `noexcept`, copy the text (truncated at 1 KiB) into a
  per-instance bounded queue (256, DropOldest) drained by the session thread into the `hio.jlink` log category.
  They never touch Qt objects. Library error-out texts are also attached to the next `hio::Error`'s `detail`.
- **`LibraryLocator`** — `[[nodiscard]] Result<LibraryLocation> locate(const LocatorOptions&)`: override
  path first; else Linux `/opt/SEGGER/JLink/libjlinkarm.so*` and `/opt/SEGGER/JLink_V*/`, macOS
  `/Applications/SEGGER/JLink/libjlinkarm.dylib` and `/Applications/SEGGER/JLink_V*/`, Windows registry
  `HKCU|HKLM\SOFTWARE\SEGGER\J-Link\InstallPath` (via `QSettings` native format) and
  `%ProgramFiles%\SEGGER\JLink*\JLink_x64.dll`; among candidates the highest version wins. Not found →
  `dependency.jlink_not_found` with the list of searched paths in `detail`.
- **`JLinkLibrary`** (RAII, only object calling the vendor library) —
  `[[nodiscard]] static Result<std::unique_ptr<JLinkLibrary>> loadCopy(const LibraryLocation&, const std::filesystem::path& instanceRoot)`:
  copy into a fresh `QTemporaryDir` (ADR-0030), load, resolve all symbols, check the version (ADR-0029),
  install the out handlers. Destructor: close if open, unload, delete the directory. `cleanupStaleInstances(root)`
  removes instance dirs older than 24 h.
- **`IJLinkApi`** — pure virtual C++ view of the table (`open`, `selectBySn`, `connect`, `hwStatus`, `rttControl`,
  `rttRead(std::span<std::byte>)`, `rttWrite(std::span<const std::byte>)`, …) implemented by `JLinkLibrary`;
  GMock'd in unit tests. All higher code depends only on `IJLinkApi`.
- **`ProbeEnumerator`** — `[[nodiscard]] Result<std::vector<ProbeInfo>> listProbes(const LibraryLocation&)`
  (`ProbeInfo { std::uint32_t serialNumber; std::string product; Connection connection; std::string firmware; }`).
  It knows nothing about locks; `libs/device` adds the lock owner (see `ProbeLock`).
- **`JLinkProbe`** (owned by the session thread) — `open(std::optional<std::uint32_t> sn)` (first probe if
  none), suppress dialogs, `setSpeed(kHz)`, SWD, `connect(deviceName)` with 3 attempts 500 ms apart,
  `hwStatus()` (target voltage mV), `firmwareStatus()` (`Current|Outdated|Newer`), `updateFirmware()`,
  `reset(halt)`, `halt()`, `go()`. Error mapping (cable diagnosis, architecture): no probe → `probe.not_found`;
  open fails → `probe.open_failed`; connect fails with VTref < 1000 mV → `target.no_power`; otherwise
  `target.connect_failed`.
- **`RttChannel`** — implements ADR-0034 exactly: `attach(names, stop_token)` (15 s deadline, 100 ms polls,
  name resolution, zero-size retry with RTT stop + 200 ms, logger wait 3 s), `read(index, span)`,
  `write(index, bytes, stop_token)` (down-buffer chunks, 5 ms retries, 2 s timeout), `stop()`.

### `libs/device`

- **Profiles** (ADR-0032): `struct TargetProfile { std::string_view id; std::string_view jlinkDevice;
  std::string_view rttTerminal; std::string_view rttLogger; bool hashPrefixIsLog; int defaultSpeedKhz;
  int defaultLatencyMs; int minTargetMillivolts; };` `struct DeviceProfile { std::string_view id;
  std::string_view displayName; std::span<const TargetProfile> targets; };` `ProfileRegistry::find(id)`.
  `profiles/chester.cpp`: `chester` / "CHESTER"; target `app` = `NRF52840_xxAA`, `Terminal`, `Logger`,
  `hashPrefixIsLog = true`, 2000 kHz, 50 ms, 1000 mV; target `lte` = `NRF9151_XXCA` naming rule (declared, unused).
- **`ConsoleLine`** — `{ std::uint64_t seq; Channel channel /*Shell, Log*/; Direction dir /*Out, In, Marker*/;
  std::chrono::system_clock::time_point time; LogLevel level /*None, Debug, Info, Warning, Error*/; std::string text; }`.
- **`LineAssembler`** (per channel): splits at the byte level on `\n`, strips a trailing `\r`, keeps an
  incomplete line (max **4 KiB**, then force-emitted), decodes UTF-8 per complete line replacing invalid
  sequences with U+FFFD (so multi-byte characters split across reads survive), strips ANSI CSI sequences,
  parses the Zephyr level tag `<dbg|inf|wrn|err>`, and (profile rule) routes shell lines starting with `#` to
  the Log channel.
- **`ConsoleLog`** — bounded store with monotonically increasing cursors: **100 000 lines per channel**
  (`RingBuffer`), `linesSince(cursor)`, `last(n)`, `clear(channelMask)` (cursors stay monotonic),
  `appendMarker(Channel, std::string)` (ADR-0063: `Direction::Marker` lines added by the GUI on connection or busy
  changes; the CLI and `--json` never produce them); used by `command` now and MCP later. Owner thread only.
- **`SessionEvent`** = `std::variant<ConsoleLine, ConnectionEvent, LinesDropped, Warning>`;
  `ConnectionEvent { ConnectionState state /*Connecting, Connected, Disconnected, Stopped*/; std::optional<Error> error; }`.
- **`DeviceSession`** (façade `QObject`, ADR-0031):
  ```cpp
  struct SessionOptions { const DeviceProfile* profile; std::string_view targetId;
      std::optional<std::uint32_t> probeSn; int speedKhz; int latencyMs; bool autoReconnect; bool resetOnOpen;
      std::optional<std::filesystem::path> consoleFile; /* nullopt = default, empty = disabled */
      std::shared_ptr<ConsoleLog> consoleLog; /* ADR-0063: nullptr = the session creates its own; a GUI tab passes
                                                 one log for all its sessions. Accessed on the owner thread only. */ };
  class DeviceSession : public QObject {
      Q_OBJECT
  public:
      // Non-blocking (ADR-0053): validates options, takes the registry slot, starts the worker, returns at once.
      [[nodiscard]] static Result<std::unique_ptr<DeviceSession>> open(SessionRegistry&, LibraryLocation, SessionOptions);
      [[nodiscard]] Status sendLine(std::string line);      // queued; Error only if the queue is full/closed
      [[nodiscard]] Status reset(bool halt);
      [[nodiscard]] Status reconnect();
      [[nodiscard]] Status suspend();                       // detach + release probe (M2 / nRF Util)
      [[nodiscard]] Status resume();
      void close();                                          // stop + join, idempotent
      [[nodiscard]] const ConsoleLog& log() const;           // owner thread only; the provided or own log
      [[nodiscard]] ProbeInfo probe() const;
  signals:
      void linesReceived(const QList<hio::device::ConsoleLine>&);
      void connectionChanged(const hio::device::ConnectionEvent&);
      void linesDropped(quint64 count);
      void warning(const hio::Error&);
  };
  // CLI helper: runs a local QEventLoop until the first Connected, or Disconnected/Stopped with an error, or timeout.
  [[nodiscard]] Status waitUntilConnected(DeviceSession&, std::chrono::milliseconds timeout);
  ```
  **Open sequence (ADR-0053).** `open()` does only the cheap, synchronous steps:
  1. validate the options (profile, target, speed and latency ranges) → `usage.invalid_argument`;
  2. take the in-process registry slot (`internal.session_limit`; same SN already open in this process →
     `probe.open_failed` "already used by this application");
  3. when `probeSn` is given, take the cross-process `ProbeLock` (ADR-0058) → `probe.in_use_by_other_process`;
  4. start the worker and return. No library loading or probe access happens on the caller's thread.

  The worker then loads the library copy, opens the probe (when no SN was given: the first probe that is not
  locked, then takes its `ProbeLock`), connects the target (3 attempts, 500 ms apart) and attaches RTT. Progress is
  reported as `Connecting` → `Connected`, or as `Disconnected` (auto-reconnect on) / `Stopped` (fatal: library,
  lock or probe errors, or any error with auto-reconnect off), with the `hio::Error`. Every step checks the stop
  token, so `close()` during connect returns within one step (≤ 1 s, apart from a single blocking vendor call).
  `probe()` returns the SN once known (`connectionChanged` is emitted after it is set). The CLI calls
  `waitUntilConnected()` (timeout = connect + attach deadline, 20 s) and ends with that error if it fails.
  Worker behaviour = ADR-0034 (write echo only after a
  successful write; disconnect on read failure; only data or a successful write clears it; power check every
  1 s; auto-reconnect every 3 s only when powered; explicit reconnect always; suspended sessions never
  re-attach automatically; state events only on change). Probe firmware `Outdated|Newer` → one `Warning`
  (`probe.firmware_outdated`), never an automatic update.
- **`SessionRegistry`** — owned by the application; max **8** sessions, one per probe SN
  (`internal.session_limit`, `probe.open_failed` "already used by this application").
  `[[nodiscard]] Result<std::vector<ProbeStatus>> listProbes(const LibraryLocation&)` wraps `listProbes` and adds,
  per probe, `inUseBy` = this process's session (tab) or another process's lock owner.
- **`ProbeLock`** (ADR-0058, RAII, move-only) — `[[nodiscard]] static Result<ProbeLock> acquire(std::uint32_t sn)`:
  `QLockFile <AppLocalData>/locks/probe-<sn>.lock`, try-lock with 0 wait, stale detection by PID; held → error
  `probe.in_use_by_other_process` with the owner's app name and PID from `getLockInfo`.
  `static std::optional<ProbeLockOwner> owner(std::uint32_t sn)` (`{std::string app; qint64 pid;}`) reads without
  taking. The lock lives as long as the session (released in `close()` / the destructor). Creating the `locks/`
  directory fails → `io.write_failed` (the session is not opened, because locking cannot be guaranteed).
- **`CommandHistory`** — max **1000** entries of max **4 KiB**, consecutive duplicates collapsed; default file
  `AppLocalDataLocation/console/history-<profile>-<target>.txt`; written atomically (`QSaveFile`) every 50 new
  entries and on close — never rewritten per command. `--history-file` overrides the path. Several processes may
  use the same file (ADR-0058): `save()` takes `QLockFile <file>.lock` (wait ≤ 2 s), re-reads the file, appends
  this instance's new entries after the ones on disk, collapses consecutive duplicates, keeps the newest 1000 and
  writes atomically. Lock timeout → that save is skipped with a log warning and retried at the next save point.
  The in-memory list is not reloaded while running (other processes' commands appear after the next start).
- **`ConsoleFileSink`** — rttt line format: `2026-10-09 12:34:56.789 > text` (shell out), ` # ` (log),
  ` < ` (in), ` @ ` (connection), header block with probe SN on open. Default: one file per session
  `AppLocalDataLocation/console/<profile>-<target>-YYYYMMDD-HHMMSS-<sn>.log`, **10 MiB** per file then `.part2`…,
  retention **50 files / 200 MiB**. Explicit `--console-file P`: append; when it exceeds **100 MiB** it is renamed
  to `P.1` (replacing an older `P.1`) and a new file starts. Write errors → one `Warning` (`io.write_failed`),
  the session continues without the file. Runs on the session worker (small buffered writes, flush each cycle).

### Error codes added (texts translatable, ADR-0027)

| Code | Category | Message / cause / suggestion | Wizard |
|---|---|---|---|
| `dependency.jlink_not_found` | Dependency | "SEGGER J-Link Software was not found." / not installed or non-standard location / "Install J-Link Software 8.00 or newer, or set its path with --jlink-library." (searched paths in detail) | FirstRunSetup |
| `dependency.jlink_load_failed` | Dependency | "The J-Link library could not be loaded." / damaged install, wrong architecture, missing symbol / "Reinstall J-Link Software." | FirstRunSetup |
| `dependency.jlink_too_old` | Dependency | "J-Link Software %1 is too old." / minimum is 8.00 / "Install the current J-Link Software." | FirstRunSetup |
| `probe.not_found` | Probe | "No J-Link probe found." (or "…with serial number %1") / USB cable, probe not powered / "Check the USB cable between the J-Link and the computer." | ConnectDevice |
| `probe.in_use_by_other_process` | Probe | "The J-Link %1 is used by another program (%2, PID %3)." / another HARDWARIO Monitor window or `hio` command / "Close the other program, or choose another J-Link." | — |
| `probe.open_failed` | Probe | "The J-Link probe could not be opened." / used by another program or session, USB permissions (Linux udev) / "Close other programs using the J-Link and try again." | Troubleshooting |
| `probe.firmware_outdated` | Probe (warning) | "The J-Link firmware is outdated." (or "newer than the library") / — / "Run `hio jlink update-firmware`." | Troubleshooting |
| `target.no_power` | Target | "The device has no power (%1 mV)." / not powered or flat cable disconnected / "Power the device and check the flat cable." | ConnectDevice |
| `target.connect_failed` | Target | "Could not connect to the device's %1 MCU." / flat cable, wrong connector, MCU in low-power or protected state / "Check the flat cable between the J-Link and the device." | ConnectDevice |
| `target.rtt_not_found` | Target | "The device's console (RTT) was not found." / firmware without RTT, still booting, CPU halted / "Reset the device or flash firmware with RTT console support." | Troubleshooting |
| `target.rtt_buffer_not_found` | Target | "RTT buffer \"%1\" not found." / older or different firmware / "Use firmware with the CHESTER shell." | Troubleshooting |
| `target.rtt_write_timeout` | Target | "The device is not reading commands." / firmware hung or shell disabled / "Reset the device." | Troubleshooting |
| `target.rtt_read_failed` | Target | "Reading from the device failed." / cable disconnected, reset / "Check the cables; the console reconnects automatically if enabled." | Troubleshooting |
| `internal.session_limit` | Internal | "Too many devices are open (max 8)." / — / "Close a device first." | — |
| `usage.confirmation_required` | Usage | "This operation needs confirmation." / non-interactive / "Re-run with --yes." | — |

### Threading summary

GUI/CLI thread: façade, registry, history, ConsoleLog. Session worker (one per session): the library copy,
`JLinkProbe`, `RttChannel`, `LineAssembler`, `ConsoleFileSink`. Probe enumeration: called synchronously on a
caller-owned thread (CLI main thread; GUI will use a worker in M3). No shared mutable state except the two
`BoundedQueue`s and the façade's atomic wake flag.

### Bounds (all documented here)

Command queue 256 · event queue 10 000 (DropOldest + marker) · J-Link log callback queue 256, 1 KiB/message ·
partial line 4 KiB · ConsoleLog 100 000 lines/channel · history 1000 × 4 KiB · console files 10 MiB/file,
50 files / 200 MiB, explicit file 100 MiB + one backup · 8 sessions/process · lock waits: probe 0 ms, history 2 s · `waitUntilConnected` 20 s · RTT read per cycle ≤ up-buffer
size (capped at 64 KiB) · timeouts: attach 15 s, logger wait 3 s, write 2 s, connect 3 × 500 ms,
power check 1 s, auto-reconnect 3 s, `command` default 1 s.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| J-Link API declarations | own minimal / vendor SEGGER header / header from install | own minimal | ADR-0028 |
| Probes per process | one / many via library copies | many via copies (max 8) | ADR-0030 |
| Minimum J-Link Software | 7.94 / 8.x / none | 8.00 | ADR-0029 |
| rttt reference | download / user path / without | rttt 1.9.2 sdist read | open-questions #12 |
| Session threading | jthread + queues + façade / QThread + event loop | jthread + queues + façade | ADR-0031 |
| Device profiles | compiled-in C++ / JSON files | compiled-in C++ | ADR-0032 |
| CLI console UX | replxx line mode / FTXUI TUI / plain | replxx line mode | ADR-0033 |
| RTT stop | follow rttt 1.9.2 / never call | follow rttt 1.9.2 | ADR-0034 |
| Probe firmware outdated | detect + explicit command / report only / prompt | detect + `hio jlink update-firmware` (GUI button M4) | — |
| History / console files | app data, bounded / py-hardwario paths | app data, bounded; options kept | — |
| Several probes, no `-n` | first probe / error | first probe, SN printed + logged | — |
| Plan split | one / two / three plans | one phased plan | — |
| New dependency replxx (BSD-3) | approve / reject | _pending (approval of this plan)_ | ADR-0033 |
| `open()` blocking (M3 review) | non-blocking + `waitUntilConnected` / GUI opener thread | non-blocking | ADR-0053 |
| Console log ownership (M3 review) | provided by owner / GUI copy / clear on new session | provided by the owner (`SessionOptions::consoleLog`), marker lines | ADR-0063 |
| Several processes (M3 review) | probe locks + history merge / no coordination | cross-process probe locks in the backend; history merged under a file lock | ADR-0058 |
| Minor defaults: all bounds and timeouts above, ANSI stripping, U+FFFD decoding, `--coredump-file` accepted and ignored, new `hio jlink` command group | accept / change | _pending_ | — |

## Implementation steps

Phase A — J-Link library
- [ ] A1 `jlink_abi.h` + `JLinkLibrary` (copy, load, resolve, version check, out handlers) + `cleanupStaleInstances`
- [ ] A2 `LibraryLocator` (3 OSes, override precedence) + tests
- [ ] A3 `IJLinkApi`, `ProbeEnumerator`
- [ ] A4 Add error codes to the `libs/core` table (+ `cs` translations)

Phase B — probe and RTT
- [ ] B1 `JLinkProbe` (open/first probe, speed, SWD, connect retries, hw status, firmware status/update, reset) + GMock tests
- [ ] B2 `RttChannel` (ADR-0034 attach/read/write/stop) + GMock tests for every rule in ADR-0034

Phase C — device model
- [ ] C1 `DeviceProfile`, `ProfileRegistry`, `profiles/chester.cpp` + tests
- [ ] C2 `LineAssembler`, `ConsoleLine` (incl. `Marker`), `ConsoleLog` (incl. `clear`, `appendMarker`, ADR-0063) + tests
- [ ] C3 `CommandHistory` (incl. locked merge-on-save, ADR-0058), `ConsoleFileSink` (rotation/retention) + tests

Phase D — session
- [ ] D1 `DeviceSession` worker + façade with non-blocking `open()` and `waitUntilConnected()` (ADR-0053), `SessionRegistry` (+ `listProbes` with `inUseBy`), `ProbeLock` (ADR-0058) + tests (TSan)

Phase E — fake library
- [ ] E1 `tests/fake_jlink/` shared library + scenario file format (env `HIO_FAKE_JLINK_SCENARIO`): probes, VTref timeline, RTT buffers/names/data, stale block, write stall, read errors, reset
- [ ] E2 Integration tests through the real locator/loader/copies, two sessions concurrently

Phase F — CLI
- [ ] F1 CPM: replxx (record version + SHA-256 below)
- [ ] F2 `--jlink-library`, `hio jlink list|info|update-firmware`
- [ ] F3 `hio chester app command`
- [ ] F4 `hio chester app console` (replxx UI, plain/NDJSON mode)
- [ ] F5 CLI end-to-end tests (QProcess + fake library): human and `--json` outputs, exit codes, Ctrl+C

Phase G — hardware and docs
- [ ] G1 HIL tests `tests/integration/` (opt-in `HIO_HIL=1`)
- [ ] G2 Manual checks on Linux, Windows, macOS; record the lowest tested J-Link version (ADR-0029)
- [ ] G3 Update CLAUDE.md "Commands", `plan/0006-cli.md` command table status, roadmap M1 → Done

| Dependency | Version | SHA-256 |
|---|---|---|
| replxx | _tbd_ | _tbd_ |

## Tests and verification

- **Unit (GTest/GMock on `IJLinkApi`):** locator precedence and version choice; version check; probe open
  (first / by SN / not found / busy); connect retries and cable diagnosis (VTref); RTT attach — buffers by
  name, buffers moved, stale zero-size block → RTT stop + retry, logger missing → continue after 3 s,
  15 s deadline → `target.rtt_not_found`; write chunking, 0-byte retry, 2 s timeout, echo only on success;
  read failure → Disconnected, data → Connected; power loss after a healthy reading, probe reporting 0 V
  always; auto-reconnect only when powered; suspend blocks auto-reconnect; state events de-duplicated.
- **Console log (ADR-0063):** a provided log receives the lines of two consecutive sessions; markers; `clear`
  keeps cursors monotonic; `nullptr` → own log.
- **Line handling:** UTF-8 split across reads, invalid UTF-8 → U+FFFD, `\r\n`, 4 KiB partial line, ANSI
  stripping, level parsing, `#` rule, bounded ConsoleLog cursors after wrap.
- **Session (TSan):** concurrent `sendLine` / `close`, queue full → `internal.queue_full`, event overflow →
  `LinesDropped`, close joins within 1 s; `open()` returns without touching the library (fake library with a
  5 s connect delay: `open()` < 50 ms); `close()` during each connect step; `waitUntilConnected` success, failure
  and timeout; events `Connecting` → `Connected` / `Stopped` with each fatal error.
- **Locks (ADR-0058):** `ProbeLock` held → second acquire fails with owner info; stale lock (dead PID) taken over;
  first-probe selection skips locked probes; history merge of two writers (no lost entries, bound kept, lock
  timeout → skipped save).
- **Fake library integration:** real copy + load of the fake library; two sessions at once; stale instance cleanup.
- **CLI end-to-end:** `hio jlink list --json` with 0/2 fake probes (one locked by a second process → `inUseBy`);
  a second `hio chester app console -n SN` while the first runs → exit 5 `probe.in_use_by_other_process`; `hio chester app command version --json`
  returns the scripted output; `console --json` with piped stdin; `--jlink-library /nonexistent` → exit 4;
  missing probe → exit 5; no power → exit 6; Ctrl+C → exit 130 without hang.
- **Sanitizers / static analysis / translations:** as in M0 (ASan+UBSan, TSan, clang-tidy, clang-format,
  `.ts` freshness).
- **HIL (CHESTER + J-Link, J-Link Software 8.x):** console attach without reset; `hio chester app command version`;
  reset from the device shell → automatic re-attach; power-cycle → Disconnected "no power" → re-attach with
  `--auto-reconnect`; reflash with JLinkExe while attached + F4 → re-attach (buffers re-resolved); two
  CHESTERs with two probes in one process; outdated probe firmware warning.
- **Manual:** the CLI checks on Linux, Windows and macOS, done once by the user before Done.

## Risks and open points

- **J-Link 8.x vs. rttt/pylink behaviour** (observed with 7.x): verified by HIL; deviations → stop and update ADR-0034.
- **Library copies** (ADR-0030): Windows antivirus locking the copy, disk usage, simultaneous USB access from
  several instances; fallback by new ADR.
- **Struct layouts / calling conventions** of own declarations (ADR-0028): `static_assert`s + HIL on all 3 OSes.
- **replxx** with clang-cl and sanitizers; terminal handling on Windows consoles.
- **Vendor library internal threads** vs. TSan: suppressions documented one by one (HIL only; the fake library is ours).
- **macOS/Linux USB permissions** (udev rules) surface as `probe.open_failed`; the setup wizard is M4.
