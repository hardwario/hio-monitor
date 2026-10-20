# 0002 — M1 J-Link & RTT: runtime J-Link library, probes, DeviceSession, RTT console, CLI

- **Status:** In progress (implementation started 2026-10-09; approved by Matej Strnad, 2026-10-09; re-approved 2026-10-09 with the ADR-0053/0058/0063 amendments); amended by 0007 (approved by Matej Strnad 2026-10-16 together with 0007–0010)
- **Milestone:** M1
- **Author:** Claude (with Matej Strnad), 2026-10-09
- **Related ADRs:** 0004, 0006, 0007, 0010, 0015, 0017, 0020, 0022–0026, 0027–0034, 0053, 0058, 0063, 0064, 0069, 0072, 0090, 0110
- **Depends on:** 0001 (M0 skeleton, incl. the translation infrastructure)

> **Amended by ADR-0053, ADR-0058 and ADR-0063 (M3 planning, applied in this text):** `DeviceSession::open()` is
> non-blocking and the CLI waits with `waitUntilConnected()`. Probes are locked across processes (`ProbeLock`,
> `probe.in_use_by_other_process`). Command-history saves are merged under a file lock. The `ConsoleLog` can be
> provided by the session's owner, and marker lines exist.

> **Amended by 0007 (approved 2026-10-16; ADR-0069, ADR-0072, ADR-0067; applied in this text):** probe-only
> sessions (`SessionOptions::connectTarget = false`) with the new state `ProbeOpen`, and
> `DeviceSession::startConnection(attachRtt)`, which promotes such a session to a normal connection while the probe
> and its `ProbeLock` stay held; the `Diagnose` operation itself is part of 0003. On Linux a failed probe open checks
> USB access and reports `usb.access_denied` (ADR-0072) when that is the cause. `probe.in_use_by_other_process` gets
> the Troubleshooting wizard. The implementation is tracked in 0007 (steps A3, B1, B2).

> **Pending amendment from M5 (ADR-0079, ADR-0083, ADR-0128, specified in [0011](0011-m5-ble-backend-cli.md); approved
> 2026-10-19 by Matej Strnad together with 0011; applied in the text by the 0011 implementation, 2026-10-19):** `DeviceSession` derives from the new
> abstract `ConsoleSession` (identity, capabilities, `promptSeen`); `waitUntilConnected` / `waitForFirstAttempt` and
> the CLI console take `ConsoleSession&`; `ConnectionEvent` gains `transport` and `mtu`; `SessionRegistry` is keyed by
> `SessionKey = std::variant<ProbeKey, BleKey>` (J-Link and BLE share the 8-session limit); `ConsoleFileSink` opens by
> `SessionKey` and its retention asks `ProbeLock` or `BleLock`; `LineAssembler` gains a read-only peek of the partial
> line.

> **Pending amendment from M6 (ADR-0099, ADR-0104, specified in [0013](0013-m6-mcp-core-cli.md) § New `libs/device`
> operations; applied to this text when 0013 is approved — then this plan needs re-approval):** new JLinkARM
> declarations (halt/go/IsHalted/GetId/WriteMem/WriteU16/U32/ReadMemU16/U32/register list, names and reads,
> GetMemZones); `TargetProfile::ram`, `peripherals`, `shellPrompt` and RTT prompt detection (`promptSeen` also for
> J-Link); `SessionOptions::rttAddress`; `releaseProbe` / `reacquireProbe`.

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
Using J-Link 801234567 (firmware 2024-10-01), library 8.10a
Connecting to NRF52840_xxAA at auto speed…
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
| `hio chester app console [--reset] [--latency MS=50] [--history-file P] [--console-file P] [-n/--jlink-sn SN] [--jlink-speed auto\|KHZ=auto] [--auto-reconnect] [--coredump-file P] [--linger S=1.0]` | Interactive console (ADR-0033). `--reset` resets the target (no halt) before attaching. Without `--auto-reconnect` a dropped link stays down until F4, and any first-attempt error ends the console. With `--auto-reconnect` Target-category errors of the first attempt do not: the console starts, shows Disconnected with the error and keeps retrying (library, probe, lock and I/O errors stay fatal). This is our own rule (decided 2026-10-09; R50), not rttt's: rttt and py-hardwario end on a probe-open or target-connect failure even with `--auto-reconnect`, and only their RTT attach is retried. `--linger S` (0–60 s, default 1.0; non-interactive and `--json` mode only, ignored interactively): after the end of stdin the console waits until the sent commands were written (≤ 3 s), then collects output for S seconds and exits 0. `--coredump-file` is accepted for py-hardwario compatibility, has no effect and logs a warning. `--console-file ''` disables the console log file. Exit 0 on user quit; an attach error is an `hio::Error` with its exit code; a closed stdout (`… --json \| head`) ends it like Ctrl+C: exit 130 `cancelled` with the message "The output was closed." (R44), the session closes and the console file is flushed (SIGPIPE ignored, R32); it is detected on the next write to stdout after the reader went away. |
| `hio chester app command CMD [--reset] [-t/--timeout S=1.0] [--console-file P] [-n SN] [--jlink-speed auto\|KHZ=auto]` | py-hardwario semantics: attach, send each line of `CMD`, collect terminal output for the timeout, detach. The timeout is counted from the last echo, i.e. from when the last line was written; the lines are written before that within at most lines × RTT write timeout (2 s) + 500 ms (R7, R23, R39, R55). Human: output lines on stdout. `--json`: `data = {"command": "…", "output": ["…"], "log": ["…"], "truncated": false}`. The response is bounded (R42): at most 10 000 lines (output + log) and 4 MiB of text; beyond that the first lines are kept, the collection ends, `truncated` is `true` and human mode prints "Output truncated after N lines" on stderr. |
| `hio jlink list` | Enumerate probes (separate short-lived library instance). `list` never opens a probe, so the firmware is known only when the library lists it (IP probes); unknown values show as `—` (human) and `null` (`--json`); `hio jlink info` shows the firmware (decided 2026-10-09). Human: table SN / product / connection / firmware / in use (e.g. "hio-monitor, PID 1234", ADR-0058). `--json`: `data = {"library": {"path","version"}, "probes": [{"serialNumber","product","connection":"usb"\|"ip","firmware":string\|null,"inUseBy":{"app","pid"}\|null}]}`. No probe → empty list, exit 0. |
| `hio jlink info [-n SN]` | Library path + version, probe SN, product, firmware string, `firmwareStatus` (`current`/`outdated`/`newer`), target voltage (mV). |
| `hio jlink update-firmware [-n SN] [--yes]` | Update the probe firmware to the one bundled in the library (or synchronize a newer one). Asks for confirmation on a TTY; non-TTY requires `--yes` (else `usage.confirmation_required`). |
| global `--jlink-library PATH` | Library override; precedence: this option > env `HIO_JLINK_LIBRARY` > setting `jlink/libraryPath` > auto-discovery. |

Option ranges (decided 2026-10-09): `--jlink-speed` 5–50 000 kHz, `--latency` 1–1000 ms (else `usage.invalid_argument`).
`--jlink-speed` also accepts `auto`, the default (ADR-0090, decided 2026-10-11): the automatic speed (`JLINKARM_SetSpeed(0)`) as pylink's
`JLink.connect(chip, speed='auto')`; an explicit kHz value forces that fixed speed for the connect and the session.

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
  (before Open: `DisableAutoUpdateFW`, `SuppressControlPanel`, `SetBatchMode = 1`, `HideDeviceSelection = 1`,
  `DisableInfoWinFlashDL`; right after Open: `DisableAutoUpdateFW`, `SetBatchMode = 1`, `HideDeviceSelection = 1`,
  `DisableInfoWinFlashDL`; `Device = <name>`; `InvalidateFW` only for `hio jlink update-firmware`. `SilentUpdateFW` is
  never sent — decided 2026-10-10, review fix R1),
  `JLINKARM_TIF_Select`, `JLINKARM_SetSpeed`, `JLINKARM_Connect`, `JLINKARM_IsConnected`, `JLINKARM_GetHWStatus`,
  `JLINKARM_GetFirmwareString`, `JLINKARM_GetEmbeddedFWString`, `JLINKARM_UpdateFirmwareIfNewer`, `JLINKARM_GetSN`,
  `JLINKARM_Reset`, `JLINKARM_Halt`, `JLINKARM_Go`, `JLINKARM_SetResetDelay`,
  `JLINKARM_EnableLog` (HIL H1: no `JLINKARM_SetLogOutHandler` export in 9.84), `JLINKARM_SetWarnOutHandler`, `JLINKARM_SetErrorOutHandler`,
  `JLINK_RTTERMINAL_Control`, `JLINK_RTTERMINAL_Read`, `JLINK_RTTERMINAL_Write` (`JLINKARM_ResetNoHalt` was removed
  from this list, decided 2026-10-09: a reset without halt is `JLINKARM_Reset` + `JLINKARM_Go`, as rttt / pylink do); structs
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
  none), suppress dialogs, `setSpeed(kHz | auto)` (auto = `JLINKARM_SetSpeed(0)`, the default; applied again right
  after `Device =` in `connect()`, as pylink, ADR-0090), SWD, `connect(deviceName)` with 3 attempts 500 ms apart,
  `hwStatus()` (target voltage mV), `firmwareStatus()` (`Current|Outdated|Newer`), `updateFirmware()`,
  `reset(halt)`, `halt()`, `go()`. Error mapping (cable diagnosis, architecture): no probe → `probe.not_found`;
  open fails → `probe.open_failed`; connect fails with VTref < 1000 mV → `target.no_power` (a reading of
  exactly 0 mV only after a healthy reading on this probe in this session: a probe that cannot measure VTref
  reports 0; review R21); otherwise `target.connect_failed`. A serial number that is not listed is checked
  against the network J-Links only on its first failed open per `JLinkProbe` (once per session or tool run,
  review R22).
- **`RttChannel`** — implements ADR-0034 exactly: `attach(names, stop_token)` (15 s deadline at auto speed and at
  1000 kHz or more; at a fixed lower speed `rttAttachDeadline()` = 15 s × 1000 / kHz, at most 60 s, ADR-0110; 100 ms polls,
  name resolution, zero-size retry with RTT stop + 200 ms, logger wait 3 s), `read(index, span)`,
  `write(index, bytes, stop_token)` (down-buffer chunks, 5 ms retries, 2 s timeout), `stop()`.

### `libs/device`

- **Profiles** (ADR-0032): `struct TargetProfile { std::string_view id; std::string_view jlinkDevice;
  std::string_view rttTerminal; std::string_view rttLogger; bool hashPrefixIsLog; std::optional<int> defaultSpeedKhz /* nullopt = auto, ADR-0090 */;
  int defaultLatencyMs; int minTargetMillivolts; };` `struct DeviceProfile { std::string_view id;
  std::string_view displayName; std::span<const TargetProfile> targets; };` `ProfileRegistry::find(id)`.
  `profiles/chester.cpp`: `chester` / "CHESTER"; target `app` = `NRF52840_xxAA`, `Terminal`, `Logger`,
  `hashPrefixIsLog = true`, automatic speed (ADR-0090), 50 ms, 1000 mV; target `lte` = `NRF9151_XXCA` naming rule (declared, unused).
- **`ConsoleLine`** — `{ std::uint64_t seq; Channel channel /*Shell, Log*/; Direction dir /*Out, In, Marker*/;
  std::chrono::system_clock::time_point time; LogLevel level /*None, Debug, Info, Warning, Error*/; std::string text; }`.
- **`LineAssembler`** (per channel): splits at the byte level on `\n`, strips a trailing `\r`, keeps an
  incomplete line (max **4 KiB**, then force-emitted), decodes UTF-8 per complete line replacing invalid
  sequences with U+FFFD (so multi-byte characters split across reads survive), strips ANSI CSI sequences,
  parses the Zephyr level tag `<dbg|inf|wrn|err>`, and (profile rule) routes shell lines starting with `#` to
  the Log channel. *Amended by 0011 (M5):* `partial()` gives a read-only view of the incomplete line (BLE prompt
  detection, ADR-0082).
- **`ConsoleLog`** — bounded store with monotonically increasing cursors: **100 000 lines per channel**
  (`RingBuffer`), `linesSince(cursor)`, `last(n)`, `clear(channelMask)` (cursors stay monotonic),
  `appendMarker(Channel, std::string)` (ADR-0063: `Direction::Marker` lines added by the GUI on connection or busy
  changes; the CLI and `--json` never produce them); used by `command` now and MCP later. Owner thread only.
- **`SessionEvent`** = `std::variant<ConsoleLine, ConnectionEvent, LinesDropped, Warning>`;
  `ConnectionEvent { ConnectionState state /*Connecting, Connected, Disconnected, Stopped*/; std::optional<Error> error; }`.
  *Amended by 0007 (2026-10-16):* `ConnectionState` gains `ProbeOpen` (between `Connecting` and `Connected`): the
  library is loaded, the probe is open and locked, the target is not connected. It is the terminal state of a
  probe-only session (`connectTarget = false`) until `startConnection()`; the CLI console never reports it.
  *Amended by ADR-0091 (2026-10-12):* `ConnectionEvent` also carries `std::optional<int> targetMillivolts`, the
  VTref of the worker's last power reading (set at least on Connected; empty when not measured).
  *Amended by 0011 (M5, ADR-0079):* `ConnectionEvent` also carries `Transport transport` (`JLink` default, `Ble`) and
  `std::optional<std::uint16_t> mtu` (BLE, set on Connected); `ProbeOpen` is never used by BLE.
  *Amended by ADR-0093 (2026-10-12):* `libs/hw/jlink` also offers `ProbeEnumerator`, a kept library copy for repeated
  enumerations (the GUI probe scanner, ADR-0050); `enumerateProbes()` is the one-shot form built on it.
- **`DeviceSession`** (façade `QObject`, ADR-0031):
  ```cpp
  struct SessionOptions { const DeviceProfile* profile; std::string_view targetId;
      std::optional<std::uint32_t> probeSn; std::optional<int> speedKhz /* nullopt = auto, ADR-0090 */; int latencyMs; bool autoReconnect; bool resetOnOpen;
      std::optional<std::filesystem::path> consoleFile; /* nullopt = default, empty = disabled */
      std::shared_ptr<ConsoleLog> consoleLog; /* ADR-0063: nullptr = the session creates its own; a GUI tab passes
                                                 one log for all its sessions. Accessed on the owner thread only. */
      bool connectTarget = true; /* amended by 0007 (ADR-0069): false = probe-only session (ProbeOpen), see below */ };
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
      // Amended by 0007 (ADR-0069): promotes a probe-only session (ProbeOpen) to a normal one — connect the target,
      // attach RTT when asked, start power polling and auto-reconnect per SessionOptions. The probe and its
      // ProbeLock stay held. Any other state → usage.invalid_argument.
      [[nodiscard]] Status startConnection(bool attachRtt);
      void close();                                          // stop + join, idempotent
      [[nodiscard]] const ConsoleLog& log() const;           // owner thread only; the provided or own log
      [[nodiscard]] ProbeInfo probe() const;
      [[nodiscard]] std::filesystem::path consoleFile() const; // amended by 0005, see below
  signals:
      void linesReceived(const QList<hio::device::ConsoleLine>&);
      void connectionChanged(const hio::device::ConnectionEvent&);
      void linesDropped(quint64 count);
      void warning(const hio::Error&);
  };
  // CLI helper: runs a local QEventLoop until the first Connected, or Disconnected/Stopped with an error, or timeout.
  [[nodiscard]] Status waitUntilConnected(DeviceSession&, std::chrono::milliseconds timeout);
  ```
  *Amended by 0011 (M5, ADR-0079):* `DeviceSession` derives from the abstract `ConsoleSession` (`console_session.h`:
  `sendLine`, `reconnect`, non-virtual idempotent `close()` → `closeSession()`, `log`, `connection`, `identity`
  `{JLink, "<sn>", "J-Link <sn>"}`, `capabilities` (Reset, LogChannel, Operations), `profile`, `consoleTargetId`
  (its target), `consoleFile`; the signals above plus `promptSeen()`, never emitted for J-Link). The J-Link
  operations, `suspend`/`resume`, `probe()`, `diagnose` and `startConnection` stay on `DeviceSession`.
  `waitUntilConnected` / `waitForFirstAttempt` and the CLI console (`ConsoleView`, `runConsole`,
  `waitForConsoleConnection`, the command collector) take `ConsoleSession&`.
  *Amended by 0005 (2026-10-16, user decision):* `consoleFile()` returns the console log file the session writes now
  (empty before the worker opened it, without a console file, and after a write failure stopped it). The worker
  reports it with a `ConsoleFileChanged` event (opened, continued in a `.partN` file, stopped) that the owner thread
  applies like `ProbeIdentified`; owner thread only. The GUI's "Open log file" uses it.
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
  `probe()` returns the SN once known (`connectionChanged` is emitted after it is set).
  *Amended by 0007 (2026-10-16, ADR-0069):* with `connectTarget = false` the worker stops after the probe open and
  the `ProbeLock` and reports `ProbeOpen`: no target connect, no RTT, no power polling, no auto-reconnect; only
  operations (0003, incl. `Diagnose`) touch the target. The `Diagnose` `connect` step uses the session's `speedKhz`
  (automatic when `nullopt`, ADR-0090), one attempt. `startConnection(attachRtt)` then continues with the target
  connect and the RTT attach as above. *Amended by 0007 (2026-10-16, ADR-0072):* on Linux a failed probe open runs
  the USB access check (`checkUsbAccess`, 0007) on the worker and reports `usb.access_denied` instead of
  `probe.open_failed` when a SEGGER USB device is not accessible. The CLI calls
  `waitUntilConnected()` (timeout `connectTimeoutForSpeed(speed)` = `connectTimeoutFor(ProbeTiming, RttTiming)` with
  the attach deadline of the session speed: 35.5 s at auto and at 1000 kHz or more, up to 80.5 s at 250 kHz and
  below, ADR-0110; see Bounds; review R38) and ends with that error if it fails. Below 125 kHz the CLI warns before
  connecting (ADR-0110).
  Amendment (decided 2026-10-09; our rule, not rttt's, R50): `console --auto-reconnect` waits with `waitForFirstAttempt()`
  instead, which also accepts a first attempt that ends Disconnected; the console UI then starts and shows the state
  while the session retries. ADR-0053's "the CLI waits with waitUntilConnected" therefore holds for `command` and
  for `console` without `--auto-reconnect`. A `Stopped` event without an error (suspended on purpose) ends both
  waits as `cancelled` (review fix R9). When either wait fails (the session's error, `timeout` or `cancelled`), the
  `--json` console still emits its one terminal `stopped` event before the error envelope (review R34).
  Worker behaviour = ADR-0034 (write echo only after a
  successful write; disconnect on read failure; only data or a successful write clears it; power check every
  1 s; auto-reconnect every 3 s only when powered; explicit reconnect always; suspended sessions never
  re-attach automatically; state events only on change). The next automatic retry is due one reconnect interval
  after the previous attempt **ended**, whatever its outcome (review R31). Probe firmware `Outdated|Newer` → one
  `Warning` (`probe.firmware_outdated`), never an automatic update.

  **Differences from rttt 1.9.2** (`PyLinkRTTConnector`, `rttt/cli.py`, over pylink-square 2.0.2; kept on purpose and
  documented, decided 2026-10-11 after the round-4 review; corrected and completed after round 6, R48–R56, each
  statement checked against the rttt and pylink sources):
  - A halt reset (`reset(halt = true)`) is not re-attached automatically: the session stays Disconnected until F4
    (reconnect), `resume()` or a reset without halt (R25). rttt's watchdog re-attaches it when `--auto-reconnect` is
    on; without it rttt stays down too.
  - Reconnect / F4 closes and reopens the probe (library copy kept), so a replugged probe is found; rttt only
    reconnects the target on the open probe (its `jlink.open()` on the open handle only increments pylink's
    reference count).
  - A logger buffer name that is not registered (terminal found) continues without logs after 3 s with one warning;
    rttt keeps searching and fails the attach after its 15 s deadline. A registered logger buffer of size 0 continues
    without logs after 3 s in both.
  - A reset while detached (probe open, RTT not attached) also attaches RTT; rttt only resets.
  - The target voltage is checked every 1 s, also while data arrives (R13); rttt checks it only on idle cycles.
  - Received data reports Connected only while the target is powered (R13, R33).
  - A failed status (VTref) read does not block the auto-reconnect retry (review fix 2).
  - `resume()` re-attaches at once.
  - `suspend()` releases the probe (closes it) until `resume()`.
  - An RTT read per cycle and channel is capped at 64 KiB.
  - Connect speed: both connect at the automatic speed by default (ADR-0090): pylink's `JLink.connect(chip)` calls
    `JLINKARM_SetSpeed(0)` after `Device =`, overriding rttt's and py-hardwario's `set_speed(2000)`. An explicit
    `--jlink-speed` kHz value is a fixed speed here; in rttt / py-hardwario it is overridden by that auto.
  - The first connect with `--auto-reconnect`: Target-category errors (no power, connect failed, RTT not found) are
    not fatal here (R50). rttt (and py-hardwario) end on a probe-open or target-connect failure even with
    `--auto-reconnect`; only the RTT attach of `connector.open()` is retried.
  - `--reset`: rttt (`rttt/cli.py`) and py-hardwario `chester app command --reset` wait 1 s after the reset before
    starting RTT; py-hardwario `console --reset` resets through nrfjprog before it opens the probe. Ours attaches right
    after the reset (the attach polls for the control block, up to 15 s). Not changed in round 6; adding the wait is
    left to a decision.
  - The down buffer is resolved by name (`Terminal`), falling back to the terminal's up index; rttt always uses the
    terminal's up index.
  - State changes are de-duplicated by state and error code (a changed error code while down is reported), and a
    `connecting` state exists (first connect, explicit reconnect / resume); rttt de-duplicates by up / down only and
    has no connecting state.
  - A command while detached is not written: one warning `target.not_connected` (R18). rttt still tries the write
    with the down-buffer size of its last attach and reports the library error as a disconnect (before any attach it
    only logs a zero-size buffer).
  - The probe firmware is never updated automatically: `DisableAutoUpdateFW` before and after Open, updates only by
    `hio jlink update-firmware` (R1). rttt calls pylink's `disable_dialog_boxes()`, which sends `SilentUpdateFW`, and
    leaves the library's automatic update on, so the firmware may be updated silently on open.
  The RTT write (2 s timeout, 5 ms retries) and the stale-control-block retry (RTT stop, 200 ms, search again) match
  rttt and are not differences.
- **`SessionRegistry`** — owned by the application; max **8** sessions, one per probe SN
  (`internal.session_limit`, `probe.open_failed` "already used by this application").
  *Amended by 0011 (M5, ADR-0083):* keyed by `SessionKey = std::variant<ProbeKey{sn}, BleKey{id}>`, storing
  `ConsoleSession*`; J-Link and BLE sessions share the 8 slots; `checkAvailable(std::optional<SessionKey>)`,
  `sessionFor(const SessionKey&)` and `bleInUse(id)`; `checkAvailable(std::optional<std::uint32_t>)` and
  `sessionForProbe(sn)` stay as thin wrappers. A BLE device used by this process → `ble.in_use_by_other_process`
  "already used by this application".
  `[[nodiscard]] Result<std::vector<ProbeStatus>> listProbes(const LibraryLocation&)` wraps `listProbes` and adds,
  per probe, `inUseBy` = this process's session (tab) or another process's lock owner.
- **`ProbeLock`** (ADR-0058, RAII, move-only) — `[[nodiscard]] static Result<ProbeLock> acquire(std::uint32_t sn)`:
  `QLockFile <AppLocalData>/locks/probe-<sn>.lock`, try-lock with 0 wait, QLockFile's stale detection (on this
  host: the PID is alive and runs the recorded program) plus our own-PID rule (R15); held → error
  `probe.in_use_by_other_process` with the owner's app name and PID from `getLockInfo`.
  `static std::optional<ProbeLockOwner> owner(std::uint32_t sn)` (`{std::string app; qint64 pid; std::optional<file_time_type>
  lockedAt;}`) reads without taking and applies the same stale rules as `acquire()` (review R37): QLockFile's rules,
  mirrored per OS from the Qt 6.8.3 sources because QLockFile has no public non-acquiring stale check (R41), plus the
  own-PID rule. A lock file that exists but cannot be read is an unknown owner (`?`, PID 0), as `acquire()` reports it. The lock lives as long as the session (released in `close()` / the destructor). Creating the `locks/`
  directory fails → `io.write_failed` (the session is not opened, because locking cannot be guaranteed).
  *Amended by 0011 (M5):* the lock-file implementation is shared with `BleLock` (`locks/ble-<sanitised id>.lock`,
  same stale, own-PID and retry rules; string-keyed helper `src/lock_file.*`).
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
  retention **50 files / 200 MiB**. The retention never deletes a file a live session is writing (R45, R48): in this
  process the files a sink has open (the open-files list); for a probe (SN from the file name) whose `ProbeLock` has a
  live owner in another process per `ProbeLock::owner()`, only the newest file of that SN, and only when it was written
  since that owner took the lock (the lock file's time; 2 s tolerance). Closed `.partN` files of a live session and
  older files are pruned like any other file, so the bounds hold apart from at most one live file per live session.
  *Amended by 0011 (M5, ADR-0128):* `open()` takes a `SessionKey` (the J-Link form with the SN stays as a thin
  overload; J-Link file names unchanged); a BLE session's default file is
  `<profile>-ble-YYYYMMDD-HHMMSS-<sanitised id>.log`; the retention parses the key from the file name and asks
  `ProbeLock::owner` or `BleLock::owner` with the same live-file rules. Owner: the session worker (J-Link) or the
  BLE thread (BLE).
  Explicit `--console-file P`: append; when it exceeds **100 MiB** it is renamed
  to `P.1` (replacing an older `P.1`) and a new file starts. If that rename fails, the file is not rotated again
  in this session (one `Warning` `io.write_failed`) and is appended to up to **2 × 100 MiB**, then it is not
  written any more (one more `Warning`; review R26). Write errors → one `Warning` (`io.write_failed`),
  the session continues without the file. Runs on the session worker (small buffered writes, flush each cycle).

### Error codes added (texts translatable, ADR-0027)

| Code | Category | Message / cause / suggestion | Wizard |
|---|---|---|---|
| `dependency.jlink_not_found` | Dependency | "SEGGER J-Link Software was not found." / not installed or non-standard location / "Install J-Link Software 8.00 or newer, or set its path with --jlink-library." (searched paths in detail) | FirstRunSetup |
| `dependency.jlink_load_failed` | Dependency | "The J-Link library could not be loaded." / damaged install, wrong architecture, missing symbol / "Reinstall J-Link Software." | FirstRunSetup |
| `dependency.jlink_too_old` | Dependency | "J-Link Software %1 is too old." / minimum is 8.00 / "Install the current J-Link Software." | FirstRunSetup |
| `probe.not_found` | Probe | "No J-Link probe found." (or "…with serial number %1") / USB cable, probe not powered / "Check the USB cable between the J-Link and the computer." | ConnectDevice |
| `probe.in_use_by_other_process` | Probe | "The J-Link %1 is used by another program (%2, PID %3)." / another HARDWARIO Monitor window or `hio` command / "Close the other program, or choose another J-Link." | Troubleshooting (*amended by 0007, 2026-10-16*; was —) |
| `probe.open_failed` | Probe | "The J-Link probe could not be opened." / used by another program or session, USB permissions (Linux udev) / "Close other programs using the J-Link and try again." *Amended by 0007 (2026-10-16, ADR-0072):* on Linux the cause "USB permissions" is named only when `usb.access_denied` (0007) was not detected separately; when it is, that error is reported instead. | Troubleshooting |
| `probe.firmware_outdated` | Probe (warning) | "The J-Link firmware is outdated." (or "newer than the library") / — / "Run `hio jlink update-firmware`." | Troubleshooting |
| `target.no_power` | Target | "The device has no power (%1 mV)." / not powered or flat cable disconnected / "Power the device and check the flat cable." | ConnectDevice |
| `target.connect_failed` | Target | "Could not connect to the device's %1 MCU." / flat cable, wrong connector, MCU in low-power or protected state / "Check the flat cable between the J-Link and the device." | ConnectDevice |
| `target.rtt_not_found` | Target | "The device's console (RTT) was not found." / firmware without RTT, still booting, CPU halted / "Reset the device or flash firmware with RTT console support." | Troubleshooting |
| `target.rtt_buffer_not_found` | Target | "RTT buffer \"%1\" not found." / older or different firmware / "Use firmware with the CHESTER shell." | Troubleshooting |
| `target.rtt_write_timeout` | Target | "The device is not reading commands." / firmware hung or shell disabled / "Reset the device." | Troubleshooting |
| `target.rtt_read_failed` | Target | "Reading from the device failed." / cable disconnected, reset / "Check the cables; the console reconnects automatically if enabled." | Troubleshooting |
| `target.rtt_write_failed` | Target | "Writing to the device failed." / cable disconnected, device reset / "Check the cables; the console reconnects automatically if enabled." (added by review fix R5) | Troubleshooting |
| `probe.firmware_update_failed` | Probe | "The J-Link firmware could not be updated." / the update failed or the probe still reports a firmware different from the library's / "Disconnect and reconnect the J-Link, then run `hio jlink update-firmware` again." (review R11) | Troubleshooting |
| `probe.io_failed` | Probe | "Communication with the J-Link failed." / the J-Link did not answer: a USB glitch, or the USB cable was disconnected / "Check the USB cable between the J-Link and the computer; the console reconnects automatically if enabled." (a failed `JLINKARM_GetHWStatus` of the open probe; review R52) | Troubleshooting |
| `target.not_connected` | Target | "The device is not connected." / the console is disconnected or still reconnecting / "Reconnect the console (F4)." or, only while the session re-attaches by itself (auto-reconnect and not halted, suspended or fatally stopped), "Wait for the console to reconnect." (review R18, R35) | ConnectDevice |
| `internal.session_limit` | Internal | "Too many devices are open (max 8)." / — / "Close a device first." | — |
| `usage.confirmation_required` | Usage | "This operation needs confirmation." / non-interactive / "Re-run with --yes." | — |

### Threading summary

GUI/CLI thread: façade, registry, history, ConsoleLog. Session worker (one per session): the library copy,
`JLinkProbe`, `RttChannel`, `LineAssembler`, `ConsoleFileSink`. Probe enumeration: called synchronously on a
caller-owned thread (CLI main thread; GUI will use a worker in M3). No shared mutable state except the two
`BoundedQueue`s and the façade's atomic wake flag.

### Bounds (all documented here)

Command queue 256 · event queue 10 000 (drops console output lines (`Direction::Out`) first, R10; echoes of written
commands (`Direction::In`) are never dropped, so `command` counts every written line, R54; when no output line is
queued the oldest element of any kind is dropped and counted in `droppedCount`, which the session reports as dropped lines —
practically unreachable, state events are de-duplicated and warnings are rare; R29) · J-Link log callback queue 256, 1 KiB/message ·
partial line 4 KiB · ConsoleLog 100 000 lines/channel · history 1000 × 4 KiB · console files 10 MiB/file,
50 files / 200 MiB, explicit file 100 MiB + one backup (after a failed rotation: no further rotation, at most
2 × 100 MiB, then not written; R26) · 8 sessions/process · lock waits: probe 0 ms, history 2 s · `waitUntilConnected` / `waitForFirstAttempt`
`defaultConnectTimeout` = library load 5 s + connect attempts (3) × (one connect 3 s + retry delay 0.5 s) + attach
deadline 15 s + logger wait 3 s + margin 2 s = **35.5 s** (`connectTimeoutFor()` in `device_session.h`, R38); with a
fixed speed below 1000 kHz the attach deadline and this timeout scale (`connectTimeoutForSpeed()`, ADR-0110): 50.5 s at
500 kHz, at most **80.5 s** (attach deadline 60 s) · RTT read per cycle ≤ up-buffer
size (capped at 64 KiB) · timeouts: attach 15 s (fixed speed < 1000 kHz: 15 s × 1000 / kHz, at most 60 s; ADR-0110), logger wait 3 s, write 2 s, connect 3 × 500 ms,
power check 1 s, auto-reconnect 3 s (counted from the end of the previous attempt, R31), `command` default 1 s,
`command` echo wait one write timeout per line counted as the lines are written (line n + 1 until start + (n + 1) ×
write timeout + 500 ms), at most lines × write timeout + 500 ms (R23, R39); `command` lines beyond the command queue
wait in the CLI and are queued as the worker takes them (R39) · `command` response 10 000 lines (output + log) and
4 MiB of text, then truncated (`"truncated": true`, R42) · console-file retention may exceed 50 files / 200 MiB by at
most one live file per live session (R45, R48) · the attach deadline and the connect timeout scale with a fixed
`--jlink-speed` below 1000 kHz (ADR-0110, replacing the R47 caveat after HIL G1); below 125 kHz a search of the whole
RAM can exceed the 60 s cap: the CLI warns before connecting and `target.rtt_not_found` names the speed; the default
automatic speed (ADR-0090) is not concerned.

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
| New dependency replxx (BSD-3) | approve / reject | approved | ADR-0033 |
| replxx pin (only tag is 0.0.4 from 2021) | tag / master commit + SHA-256 / build both | master commit + SHA-256 | ADR-0064 |
| Fake J-Link scenario format | JSON via QtCore / plain text | JSON (`QJsonDocument`, ADR-0019); the fake library links QtCore | — |
| HIL hardware | here / elsewhere later | J-Link Software 8.x + probe + CHESTER on the dev machine (user installs) | — |
| `open()` blocking (M3 review) | non-blocking + `waitUntilConnected` / GUI opener thread | non-blocking | ADR-0053 |
| Console log ownership (M3 review) | provided by owner / GUI copy / clear on new session | provided by the owner (`SessionOptions::consoleLog`), marker lines | ADR-0063 |
| Several processes (M3 review) | probe locks + history merge / no coordination | cross-process probe locks in the backend; history merged under a file lock | ADR-0058 |
| Probe-only sessions and live connection checks (M4 planning) | `Diagnose` operation + `connectTarget = false` / standalone checker / interpret `open()` | `Diagnose` operation and probe-only sessions with `startConnection` (approved with 0007 on 2026-10-16) | ADR-0069 |
| Linux USB permissions (M4 planning) | detect + show commands / pkexec / docs link | detect after a failed open → `usb.access_denied` (approved with 0007 on 2026-10-16) | ADR-0072 |
| Probe firmware auto-update on open | before Open / before+after / keep SilentUpdateFW | before and after Open; SilentUpdateFW removed (decided 2026-10-10) | — |
| HIL with a single probe | one probe now / wait for two | one probe now; two-probe and outdated-firmware HIL checks deferred, still required before Done | — |
| RTT write error code (implementation review) | own code / reuse `target.rtt_read_failed` | own code `target.rtt_write_failed` (review fix R5; decided 2026-10-09) | — |
| End of input in the plain / `--json` console | fixed 1 s / option | option `--linger S` (0–60 s, default 1.0; non-interactive mode only) after the commands were written (≤ 3 s) (decided 2026-10-09) | — |
| Option ranges | as implemented / other | speed 5–50 000 kHz, latency 1–1000 ms (decided 2026-10-09) | — |
| Reset without halt | `JLINKARM_ResetNoHalt` / Reset + Go | Reset + Go as rttt; `ResetNoHalt` removed from the function list (decided 2026-10-09) | — |
| `jlink list` firmware | open probes / only what the library lists | `list` never opens probes; unknown = `—` / `null`; `jlink info` shows it (decided 2026-10-09) | — |
| `--auto-reconnect` and the first connect | fatal / not fatal for Target errors | our own rule, not rttt's (rttt ends on a probe-open or target-connect failure even with `--auto-reconnect`; only its RTT attach retries; listed in Differences from rttt, R50): Target errors of the first attempt are not fatal; the retry power gate also works before a healthy reading (after `target.no_power`); library / probe / lock / I/O errors stay fatal; `command` still needs a connect (decided 2026-10-09; reworded 2026-10-11; amends the ADR-0053 wording, see Design) | ADR-0053 (amended here) |
| Probe selection (independent review) | USB + IP / USB only | USB only for sessions, `jlink info` and `update-firmware`; `jlink list` lists USB and IP (decided 2026-10-10, R12) | — |
| Firmware update result (independent review) | trust the library / verify | verified by re-reading the status after reopening the probe; unreadable status = `unknown` (decided 2026-10-10, R11) | — |
| Event queue overflow (independent review) | drop oldest event / drop only lines | the event queue drops only console lines, oldest first (decided 2026-10-10, R10) | — |
| `ConsoleLog` allocation | up front / lazy | lazy: `RingBuffer` storage grows (doubling, bounded by the capacity) as lines arrive; `open()` < 50 ms with the session's own log (decided 2026-10-09) | — |
| Round-3 findings | fix / document | fix 1–9 and 11, document 10 (decided 2026-10-11) | — |
| Round-4 findings | fix all / partial | fix all 10 (decided 2026-10-11; R31–R40) | — |
| Differences from rttt (round-4 review) | follow rttt / keep and document | keep ours, documented (decided 2026-10-11; list in Design → `libs/device`) | — |
| Round-5 findings | fix all / partial | fix all 7 (decided 2026-10-11; R41–R47) | — |
| Connect speed | auto / fixed | auto like pylink; explicit kHz forces fixed (decided 2026-10-11) | ADR-0090 |
| RTT attach deadline at a low fixed speed (HIL G1, H2) | scale / keep 15 s and warn / document | scale: 15 s × 1000 / kHz below 1000 kHz, at most 60 s; connect timeout follows; CLI warning and speed in the error detail below 125 kHz (decided 2026-10-16) | ADR-0110 |
| Round-6 findings | fix all / partial | fix all 9 + correct the rttt list (decided 2026-10-11; R48–R56) | — |
| Sanitizer presets DEBUGINFOD_URLS | set empty / local env | set empty in CMakePresets.json (decided 2026-10-11; T1) | — |
| Reference sources location | scratchpad / ../refs | /home/hardwario/src/monitor/refs, outside repositories (decided 2026-10-11) | — |
| Minor defaults: all bounds and timeouts above, ANSI stripping, U+FFFD decoding, `--coredump-file` accepted and ignored, new `hio jlink` command group | accept / change | accepted | — |

## Implementation steps

Phase A — J-Link library
- [x] A1 `jlink_abi.h` + `JLinkLibrary` (copy, load, resolve, version check, out handlers) + `cleanupStaleInstances` —
  the final function list is in `libs/hw/jlink/src/jlink_abi.h` (source of every declaration noted there, struct
  sizes and the offsets we read `static_assert`ed). `JLINKARM_ResetNoHalt` is not used: a reset without halt is
  `JLINKARM_Reset` + `JLINKARM_Go`, as rttt / pylink do. Out handlers: a fixed table of 16 handler slots (one per
  loaded copy: 8 sessions + short-lived enumerations), queue 256 / 1 KiB per message as planned.
- [x] A2 `LibraryLocator` (3 OSes, override precedence) + tests — an override that does not exist is
  `dependency.jlink_not_found` (never silently replaced by discovery); an override may also name a directory.
  `defaultLocatorOptions()` fills env / setting / search roots; `libraryPathSetting()` = `jlink/libraryPath`.
- [x] A3 `IJLinkApi`, `ProbeEnumerator` — plus `enumerateProbes()` (also returns the loaded library version, for
  `hio jlink list`); `listProbes()` as planned is built on it.
- [x] A4 Add error codes to the `libs/core` table (+ `cs` translations) — the default-text table gained a `wizard`
  column (the wizards of the error table above); texts with values (`%1`) are built where the error is created
  (contexts `hio::jlink`, `hio::device`).

Phase B — probe and RTT
- [x] B1 `JLinkProbe` (open/first probe, speed, SWD, connect retries, hw status, firmware status/update, reset) + GMock tests
  — `JLinkProbe::hardwareStatus()` failing (probe not answering, e.g. unplugged) is `probe.not_found` (since R52:
  `probe.io_failed`).
- [x] B2 `RttChannel` (ADR-0034 attach/read/write/stop) + GMock tests for every rule in ADR-0034 — timing through an
  injectable `jlink::Clock` (simulated time in tests). The down buffer is resolved by name too (fallback: the
  terminal's up index, as rttt). A write error reported by the library is `target.rtt_write_failed` (review fix R5).

Phase C — device model
- [x] C1 `DeviceProfile`, `ProfileRegistry`, `profiles/chester.cpp` + tests
- [x] C2 `LineAssembler`, `ConsoleLine` (incl. `Marker`), `ConsoleLog` (incl. `clear`, `appendMarker`, ADR-0063) + tests
  — `ConsoleLog::append` assigns `seq` (1, 2, …; a cursor = the last seen `seq`, 0 = none), so the façade's
  `linesReceived` and `--json` carry the log's sequence numbers.
- [x] C3 `CommandHistory` (incl. locked merge-on-save, ADR-0058), `ConsoleFileSink` (rotation/retention) + tests —
  console file names use UTC time like the application logs; retention prunes `*.log` in the console directory.

Phase D — session
- [x] D1 `DeviceSession` worker + façade with non-blocking `open()` and `waitUntilConnected()` (ADR-0053), `SessionRegistry` (+ `listProbes` with `inUseBy`), `ProbeLock` (ADR-0058) + tests (TSan)
  — additive API: `DeviceSession::connection()` (last event), `profile()`, `target()`; `waitUntilConnected()` takes
  an optional `std::stop_token` (Ctrl+C); `SessionRegistry(RegistryOptions)` (instance root, lock and console
  directories; defaults in AppLocalData), `enumerateProbes()` (with the library version), `checkAvailable()`;
  `probe_tools.h` (`inspectProbe`, `updateProbeFirmware`) for `hio jlink info|update-firmware`. Option ranges:
  speed 5–50 000 kHz (pylink's limits) or auto (ADR-0090, R49), latency 1–1000 ms. The first connect is fatal (`Stopped`) for library,
  probe, lock, I/O and internal errors, and for every error without auto-reconnect; after that every failure is
  `Disconnected`. A `Stopped` session still accepts `reconnect()`. `suspend()` reports `Stopped` without error;
  `resume()` = allow attaching and re-attach now. Tests run on an in-process scripted `IJLinkApi`
  (`libs/device/tests/scripted_api.h`) with short timings; the façade wakes by a queued call by name (TSan:
  one documented Qt suppression, `race:QMetaObject::invokeMethod`, see `tools/sanitizers/tsan.supp`).

Phase E — fake library
- [x] E1 `tests/fake_jlink/` shared library + scenario file format (env `HIO_FAKE_JLINK_SCENARIO`): probes, VTref timeline, RTT buffers/names/data, stale block, write stall, read errors, reset
  — format documented at the top of `tests/fake_jlink/fake_jlink.cpp`; `hio_fake_jlink_incomplete` (one export)
  tests the missing-symbol error.
- [x] E2 Integration tests through the real locator/loader/copies, two sessions concurrently —
  `libs/device/tests/fake_library_test.cpp`, `libs/hw/jlink/tests/jlink_library_test.cpp`.

Phase F — CLI
- [x] F1 CPM: replxx (record version + SHA-256 below)
- [x] F2 `--jlink-library`, `hio jlink list|info|update-firmware` — `jlink info --json` reports the voltage as
  `targetMillivolts` (null when unknown) and also `connection` and `libraryFirmware`; `update-firmware --json`:
  `{"serialNumber","before","updated"}`.
- [x] F3 `hio chester app command` — output = shell lines received after sending (echo excluded), log = log lines;
  a connection error during the timeout ends the command with that error.
- [x] F4 `hio chester app console` (replxx UI, plain/NDJSON mode) — replxx only when stdin and stdout are
  terminals; prompt `uart:~$ `. Plain mode: end of input waits until the sent commands were written (≤ 3 s) plus
  1 s for their output, then ends with exit 0. `--json` streams the `connecting`/`connected` events too and ends
  with `{"type":"connection","state":"stopped"}`. `-n` / `--jlink-speed` are accepted on `chester app` and on
  its subcommands (the subcommand wins).
- [x] F5 CLI end-to-end tests (QProcess + fake library): human and `--json` outputs, exit codes, Ctrl+C —
  `app/cli/tests/jlink_e2e_test.cpp` (the M0 QProcess fixture moved to `cli_process.h`).

Phase G — hardware and docs
- [ ] (2026-10-12, M2 review R1) The J-Link unsecure-dialog hook (answers NO) is added on `v2/m2-flashing`; run the M1 HIL on code that contains it.
- [ ] G1 HIL tests `tests/integration/` (opt-in `HIO_HIL=1`) — written (`hio_hil_tests`, ctest label `hil`, every
  test skipped without `HIO_HIL=1`). Also measure the connect and RTT attach time at
  `--jlink-speed` auto (the default, ADR-0090) and 5, 100 and 1000 kHz against the 35.5 s connect timeout (R47).
  - Run 1 (2026-10-16; Linux, J-Link Software 9.84, one J-Link probe, one CHESTER-M nRF52840 application MCU):
    `hio_hil_tests` 4 of 5 pass (`ProbesAreListed`, `ConsoleAttachesWithoutResetAndAnswersVersion`,
    `ResetReattaches`, `RebootFromTheShellIsReattachedAutomatically`); `TwoChestersWithTwoProbesInOneProcess`
    skipped (one probe only).
  - H1 (found and fixed): J-Link 9.84 does not export `JLINKARM_SetLogOutHandler`; the log handler is registered
    with `JLINKARM_EnableLog` (A1 function list).
  - Timing: connect about 0.3 s at every speed. RTT attach: auto < 2 s (whole command 2.3 s); 1000 kHz 2.9 s
    command; 100 kHz 12.8 s RTT search (14.5 s command); 5 kHz `target.rtt_not_found` after 15 s → H2, fixed by
    ADR-0110 (attach deadline and connect timeout scale with a fixed low speed; warning below 125 kHz).
  - `hio jlink list`, `hio jlink info`, `hio chester app command help` work; target voltage read 2983 mV. The probe
    firmware status is "unknown" on this probe (`JLINKARM_GetEmbeddedFWString` returned -1), so the
    outdated-firmware check could not be tested with it.
  - Not testable with this setup: two probes in one process, the outdated-firmware warning.
  - Open: re-run G1 after the ADR-0110 fix (5 and 100 kHz) and the two-probe test with a second probe and board.
  - Run 2 (2026-10-16, after ADR-0110, merged into `v2/m2-flashing`): the five tests pass when run on their own;
    100 kHz attaches (with the slow-speed warning), 5 kHz warns and ends with `target.rtt_not_found` as designed.
  - H6 (open, intermittent; seen twice in about 15 attaches right after a flash and reset): the attach finds an
    RTT control block at `0x20000410` (very likely the bootloader's, which runs first after a reset) instead of
    the application's at `0x2000CB30`. The terminal buffer is never registered there, and stop + start of RTT in
    the same library instance keeps finding the same block, so the attach ends with `target.rtt_not_found`
    ("terminal buffer not ready within 15000 ms"). A new process (new library copy) right afterwards attaches at
    once. Suspected: the library keeps the found address across RTT stop/start. Candidate fix, to confirm on
    hardware with a reliable reproduction: re-arm the search before each restart (for example
    `SetRTTSearchRanges` with the profile's RAM range) or reload the library copy. Also seen once as a failure of
    `Hil.ConsoleAttachesWithoutResetAndAnswersVersion` when it runs right after `HilFlash.FlashByFile…` in one
    `hio_hil_tests` run.
- [ ] G2 Manual checks on Linux, Windows, macOS; record the lowest tested J-Link version (ADR-0029); on Windows
  also: `hio chester app console --json` reading from the console (not a pipe) ends promptly on Ctrl+C / when the
  session ends (review R17, `CancelSynchronousIo`); `hio jlink list` shows "in use" for a probe held by another
  `hio` / HARDWARIO Monitor process on Windows (R41)
- [x] G3 Update CLAUDE.md "Commands", `plan/0006-cli.md` command table status, roadmap M1 → Done — roadmap says
  "implemented, Done after G1/G2" (M1 is not Done before the hardware tests).

| Dependency | Version | SHA-256 |
|---|---|---|
| replxx | master commit `1f149bfe20bf6e49c1afd4154eaf0032c8c2fda2` (2021-11-25, the newest commit; 15 commits after `release-0.0.4` of 2021-10-21), archive `https://github.com/AmokHuginnsson/replxx/archive/1f149bfe20bf6e49c1afd4154eaf0032c8c2fda2.tar.gz` (ADR-0064) | `ee6d6c92efd060717c884f10293f32692bec17a2956d10cfcedfee5618c9a5ab` |

### Review fixes

The six review rounds (R1–R56, 2026-10-09 to 2026-10-11) are in
[0002-m1-jlink-rtt-reviews.md](0002-m1-jlink-rtt-reviews.md).

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
  `.ts` freshness). Verification note (T1): the sanitizer test presets set `DEBUGINFOD_URLS` empty, so a sanitizer
  report is symbolized from local debug info only (an unreachable debuginfod server made llvm-symbolizer block and the
  test was killed); run sanitizer tests through `ctest --preset …`, or set `DEBUGINFOD_URLS=` when running a test
  binary directly.
- **HIL (CHESTER + J-Link, J-Link Software 8.x):** console attach without reset; `hio chester app command version`;
  reset from the device shell → automatic re-attach; power-cycle → Disconnected "no power" → re-attach with
  `--auto-reconnect`; reflash with JLinkExe while attached + F4 → re-attach (buffers re-resolved); two
  CHESTERs with two probes in one process; outdated probe firmware warning.
  Available hardware (2026-10-09): one J-Link probe, J-Link Software 9.84 in `/opt/SEGGER/JLink`,
  one CHESTER (VTref 2.98 V). **Deferred, still required before M1 is Done:** (1) "two CHESTERs with two probes in
  one process" needs a second J-Link + second CHESTER (the test `Hil.TwoChestersWithTwoProbesInOneProcess` stays
  in `tests/integration/` and skips with fewer than two probes); (2) "outdated probe firmware warning" and (3) "probe with outdated firmware is not updated by
  `open()`" (review fix R1) need a probe with older firmware (the probe was auto-updated to the newest firmware, Sep 24 2026, by J-Link Commander).
- **Manual:** the CLI checks on Linux, Windows and macOS, done once by the user before Done.

## Risks and open points

- **J-Link 8.x vs. rttt/pylink behaviour** (observed with 7.x): verified by HIL; deviations → stop and update ADR-0034.
- **Library copies** (ADR-0030): Windows antivirus locking the copy, disk usage, simultaneous USB access from
  several instances; fallback by new ADR.
- **Struct layouts / calling conventions** of own declarations (ADR-0028): `static_assert`s + HIL on all 3 OSes.
- **replxx** with clang-cl and sanitizers; terminal handling on Windows consoles.
- **Vendor library internal threads** vs. TSan: suppressions documented one by one (HIL only; the fake library is ours).
- **macOS/Linux USB permissions** (udev rules) surface as `probe.open_failed`; the setup wizard is M4.
  *Amended by 0007 (2026-10-16):* on Linux they are detected after a failed open and reported as
  `usb.access_denied` (ADR-0072), handled by the setup wizard (0008).
- **Very low explicit SWD speeds** (R47): HIL G1 measured 12.8 s for the RTT search at 100 kHz and no control block
  within 15 s at 5 kHz (H2). Since ADR-0110 the attach deadline (15 s × 1000 / kHz, at most 60 s) and the connect
  timeout scale with a fixed speed below 1000 kHz; below 125 kHz a full-RAM search can still exceed 60 s, so the CLI
  warns before connecting and `target.rtt_not_found` names the speed. The default automatic speed (ADR-0090) is not
  concerned.
- Found during implementation (2026-10-09): replxx master is only 15 commits ahead of `release-0.0.4` and has had no
  commit since 2021-11-25 (ADR-0064 assumed "years of fixes"); the dependency is effectively unmaintained. It builds
  with one third-party compiler warning (`-Wcharacter-conversion` in `terminal.cxx`, not our flags). The other
  open points of the implementation were decided on 2026-10-09 (see the decision table).
