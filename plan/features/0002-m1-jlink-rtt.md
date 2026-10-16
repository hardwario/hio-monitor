# 0002 — M1 J-Link & RTT: runtime J-Link library, probes, DeviceSession, RTT console, CLI

- **Status:** In progress (implementation started 2026-10-09; approved by Matej Strnad, 2026-10-09; re-approved 2026-10-09 with the ADR-0053/0058/0063 amendments)
- **Milestone:** M1
- **Author:** Claude (with Matej Strnad), 2026-10-09
- **Related ADRs:** 0004, 0006, 0007, 0010, 0015, 0017, 0020, 0022–0026, 0027–0034, 0053, 0058, 0063, 0064, 0090
- **Depends on:** 0001 (M0 skeleton, incl. the translation infrastructure)

> **Amended by ADR-0053, ADR-0058 and ADR-0063 (M3 planning, applied in this text):** `DeviceSession::open()` is
> non-blocking and the CLI waits with `waitUntilConnected()`. Probes are locked across processes (`ProbeLock`,
> `probe.in_use_by_other_process`). Command-history saves are merged under a file lock. The `ConsoleLog` can be
> provided by the session's owner, and marker lines exist.

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
  `JLINKARM_SetLogOutHandler`, `JLINKARM_SetWarnOutHandler`, `JLINKARM_SetErrorOutHandler`,
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
- **`RttChannel`** — implements ADR-0034 exactly: `attach(names, stop_token)` (15 s deadline, 100 ms polls,
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
      std::optional<std::uint32_t> probeSn; std::optional<int> speedKhz /* nullopt = auto, ADR-0090 */; int latencyMs; bool autoReconnect; bool resetOnOpen;
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
  `waitUntilConnected()` (timeout `defaultConnectTimeout` = `connectTimeoutFor(ProbeTiming, RttTiming)`, 35.5 s,
  see Bounds; review R38) and ends with that error if it fails.
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
| `probe.in_use_by_other_process` | Probe | "The J-Link %1 is used by another program (%2, PID %3)." / another HARDWARIO Monitor window or `hio` command / "Close the other program, or choose another J-Link." | — |
| `probe.open_failed` | Probe | "The J-Link probe could not be opened." / used by another program or session, USB permissions (Linux udev) / "Close other programs using the J-Link and try again." | Troubleshooting |
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
deadline 15 s + logger wait 3 s + margin 2 s = **35.5 s** (`connectTimeoutFor()` in `device_session.h`, R38) · RTT read per cycle ≤ up-buffer
size (capped at 64 KiB) · timeouts: attach 15 s, logger wait 3 s, write 2 s, connect 3 × 500 ms,
power check 1 s, auto-reconnect 3 s (counted from the end of the previous attempt, R31), `command` default 1 s,
`command` echo wait one write timeout per line counted as the lines are written (line n + 1 until start + (n + 1) ×
write timeout + 500 ms), at most lines × write timeout + 500 ms (R23, R39); `command` lines beyond the command queue
wait in the CLI and are queued as the worker takes them (R39) · `command` response 10 000 lines (output + log) and
4 MiB of text, then truncated (`"truncated": true`, R42) · console-file retention may exceed 50 files / 200 MiB by at
most one live file per live session (R45, R48) · the connect timeout (35.5 s) assumes SWD speeds of about 1000 kHz
or more: a very low explicit `--jlink-speed` value (e.g. 5 kHz) may end with `timeout` before the session's own error
(R47, not scaled; measured by HIL, G1); the default automatic speed (ADR-0090) is not concerned.

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
- [ ] G1 HIL tests `tests/integration/` (opt-in `HIO_HIL=1`) — written (`hio_hil_tests`, ctest label `hil`, every
  test skipped without `HIO_HIL=1`); not yet run on hardware. Also measure the connect and RTT attach time at
  `--jlink-speed` auto (the default, ADR-0090) and 5, 100 and 1000 kHz against the 35.5 s connect timeout (R47).
- [ ] G2 Manual checks on Linux, Windows, macOS; record the lowest tested J-Link version (ADR-0029); on Windows
  also: `hio chester app console --json` reading from the console (not a pipe) ends promptly on Ctrl+C / when the
  session ends (review R17, `CancelSynchronousIo`); `hio jlink list` shows "in use" for a probe held by another
  `hio` / HARDWARIO Monitor process on Windows (R41)
- [x] G3 Update CLAUDE.md "Commands", `plan/0006-cli.md` command table status, roadmap M1 → Done — roadmap says
  "implemented, Done after G1/G2" (M1 is not Done before the hardware tests).

| Dependency | Version | SHA-256 |
|---|---|---|
| replxx | master commit `1f149bfe20bf6e49c1afd4154eaf0032c8c2fda2` (2021-11-25, the newest commit; 15 commits after `release-0.0.4` of 2021-10-21), archive `https://github.com/AmokHuginnsson/replxx/archive/1f149bfe20bf6e49c1afd4154eaf0032c8c2fda2.tar.gz` (ADR-0064) | `ee6d6c92efd060717c884f10293f32692bec17a2956d10cfcedfee5618c9a5ab` |

### Review fixes (code review 2026-10-09)

- [x] R1 Probe firmware must never update during `JLINKARM_Open` (decided 2026-10-10: before and after Open).
  `JLinkProbe::open()` sends `DisableAutoUpdateFW` plus the dialog suppression (`SuppressControlPanel`,
  `SetBatchMode = 1`, `HideDeviceSelection = 1`, `DisableInfoWinFlashDL`) before `JLINKARM_Open`, and
  `DisableAutoUpdateFW` with the suppression again right after it; `SilentUpdateFW` is removed from the open path
  (deviation from the original command list in Design, which is updated). Sources: SEGGER KB "J-Link Command
  Strings" and the command names in the installed library 9.84, which accepts them before Open (an unknown command
  is rejected); whether `DisableAutoUpdateFW` before Open survives Open is not documented, hence both. The probe
  firmware string is logged (info) after Open; any library message about a firmware update is logged as a warning
  (`JLinkLibrary::flushMessages`). `hio jlink update-firmware` keeps its explicit update. Test
  `Probe.OpenDisablesTheFirmwareUpdateBeforeAndAfterOpen` (exact call order, no `SilentUpdateFW`). Verification on
  hardware is a deferred HIL check (see Tests and verification).
- [x] R2 Auto-reconnect after a probe USB unplug/replug: a failed status read no longer blocks the retry; the probe
  counts as lost, the retry closes and reopens it (3 s cadence). Test `Session.AutoReconnectRecoversAfterProbeUnplug`.
- [x] R3 Automatic retries report neither `Connecting` nor a repeated `Disconnected` with the same error code (also
  the console file `@` lines); state or error-code changes and `Connected` are reported; an explicit reconnect /
  resume reports `Connecting`. Test `Session.AutomaticRetriesReportOnlyChanges`.
- [x] R4 `CommandHistory`: a save (with its lock wait) is attempted every 50 `add()`s by a counter, also when the
  pending list is capped. Test `History.SaveAttemptsStayAtEvery50AddsWhenSavesAreSkipped`.
- [x] R5 New code `target.rtt_write_failed` (table above, Czech texts). Tests `MakeError.RttWriteFailedCode`,
  `Rtt.WriteErrorFromTheLibrary`.
- [x] R6 Out-handler teardown: per-slot in-flight counter (sequentially consistent; the destructor nulls the queue
  pointer, then waits for in-flight handlers); a slot whose library copy cannot be unloaded is kept for the process
  lifetime (bounded by 16 slots). Tests `OutHandlers.TeardownWhileVendorThreadsAreInsideHandlers` (TSan/ASan stress),
  `OutHandlers.MessagesReachTheSlotQueueTruncated`, `OutHandlers.AtMostMaxSlots`.
- [x] R7 `chester app command`: the timeout counts from when the last line was written (its echo), at most RTT
  write timeout (2 s) + timeout. Test `FakeJLinkCli.CommandTimeoutCountsFromTheWrite`.
- [x] R8 `LineAssembler`: after ANSI stripping, trailing `\r`s are dropped and the text after the last remaining `\r`
  is kept (terminal overwrite, e.g. Zephyr prompt redraws). Test `LineAssembler.CarriageReturnsOverwriteLikeATerminal`.
- [x] R9 `waitUntilConnected()` ends with `cancelled` on `Stopped` without an error: that only happens when the
  session was stopped on purpose (`suspend()`), so it will not connect by itself and no error explains it. Test
  `Session.WaitUntilConnectedOnASuspendedSessionIsCancelled`.

Decisions of 2026-10-09 implemented with them (tests): `--linger` (`FakeJLinkCli.ConsoleLingerCollectsLateOutput`),
`--auto-reconnect` first attempt (`Session.FirstAttemptWithoutPowerWaitsForPowerWithAutoReconnect`,
`Session.WaitForFirstAttemptFailsOnFatalErrors`, `FakeJLinkCli.ConsoleWithAutoReconnectStaysOpenWithoutPower`),
`jlink list` firmware `null`/`—` (`FakeJLinkCli.JLinkListJson`), lazy `RingBuffer` (`RingBuffer.StorageGrowsLazily`;
`Session.OpenReturnsWithoutTouchingTheLibrary` uses the session's own log again). The fake library's VTref timeline
now counts from the first `JLINKARM_Open` of a copy (a reopen does not restart the target's power).

### Review fixes (independent review 2026-10-10)

- [x] R10 Event loss: `BoundedQueue` gained `OverflowPolicy::DropOldestMatching` (drop the oldest element matching a
  predicate, else the oldest); the session event queue drops only `ConsoleLine`s (bound 10 000, order kept, count
  reported as `linesDropped`). Tests `BoundedQueue.DropOldestMatchingKeepsOtherElements`,
  `Session.EventOverflowDropsOnlyConsoleLines`. Amended by R54: only console output lines (`Direction::Out`) are
  dropped; echoes (`Direction::In`) are kept.
- [x] R11 Firmware update verified: `FirmwareStatus::Unknown` (unreadable / uncomparable; `jlink info` shows
  `unknown`, no warning); `updateProbeFirmware` opens in `OpenMode::FirmwareUpdate` (no `DisableAutoUpdateFW`;
  since R24 only after a Normal open read a status other than Current), updates, closes, reopens normally and
  re-reads the status; not `current` afterwards → `probe.firmware_update_failed`.
  The reopen follows pylink `sync_firmware()` (immediately, no extra wait) — to be confirmed by HIL. Tests
  `Probe.FirmwareStatus`, `FirmwareUpdate.OutdatedIsUpdatedAndVerified`, `FirmwareUpdate.StillOutdatedAfterTheUpdateFails`,
  `FirmwareUpdate.UnknownIsUpdatedAndVerified`, `FirmwareUpdate.StillUnknownAfterTheUpdateFails`,
  `FirmwareUpdate.NewerIsSynchronizedAfterInvalidating`, `FirmwareUpdate.CurrentFirmwareIsNotUpdated`,
  `MakeError.FirmwareUpdateFailedAndNotConnectedCodes`.
- [x] R12 Network J-Links: `IJLinkApi::listProbes(ProbeHosts)`; probe selection and every probe open list USB only;
  `jlink list` shows USB + IP; `-n` of a network J-Link → `probe.not_found` "… network J-Links are not supported for
  sessions yet." Tests `Probe.ProbesAreListedFromUsbOnly`, `Probe.NetworkProbeSerialNumberIsNotSupported`,
  `FakeLibrarySession.NetworkProbesAreNotUsedForSessions`, `FakeJLinkCli.JLinkListShowsNetworkProbes`.
- [x] R13 Power loss with continuous output: a VTref reading right after the target connects arms the detection; the
  power check runs every second whether or not data arrives (data reports Connected only while powered). Test
  `Session.PowerLossIsDetectedDuringContinuousOutput`.
- [x] R14 Stale instances: every library copy holds `instance.lock` in its directory; cleanup removes a directory
  older than 24 h only when that lock can be taken (owner gone) or is missing. Tests
  `FakeLibrary.CleanupKeepsInstancesInUse`, `FakeLibrary.CleanupRemovesInstancesWithAStaleLock`,
  `FakeLibrary.StaleInstancesAreCleanedUp`.
- [x] R15 PID reuse: a process-wide registry of held probe SNs (owned by `ProbeLock`, mutex); a lock file with our
  PID for an SN we do not hold is removed (`removeStaleLockFile()`) and the lock retried once. Test
  `Lock.OwnPidLockNotHeldInProcessIsTakenOver`.
- [x] R16 `command` without a write fails: a warning `target.not_connected`, `target.rtt_buffer_not_found` or
  `target.rtt_write_*` while collecting ends it with that error; fewer echoes than lines after the write wait →
  `target.rtt_write_timeout`. Test `FakeJLinkCli.CommandThatCannotBeWrittenFails` (exit 6, error envelope).
- [x] R17 Windows stdin reader: `~StdinLineReader` calls `CancelSynchronousIo` on the reader thread (at most
  100 × 10 ms) until it has finished, then joins (`_WIN32` only; pipe path unchanged). Not testable here; manual
  check added to G2.
- [x] R18 `target.not_connected` (table above) for `sendLine` and `reset` while detached; suggestion depends on
  auto-reconnect. Tests `Session.SendWhileNotConnectedIsAWarning`, `Session.SendAfterSuspendIsNotConnected`.
- [x] R19 Timing-dependent tests: fixed sleeps in the session tests replaced by waits on observable conditions
  (ScriptedTarget counters `rttReads`, `hwStatusCalls`, `lists`, `opensStarted`, `connectsStarted`, recorder events;
  `waitUntilOutputEmitted()` for the overflow tests); the attach deadline is 5 s except in the two tests of the
  deadline (400 ms). The only remaining fixed wait shows that a suspended session does nothing (no observable
  activity exists). Correction of the earlier report: TSan was not reliably green — `Session.EventOverflowIsReportedAsDroppedLines`
  failed about 1 in 5 runs (400 ms attach deadline). Repeat runs (2026-10-10): `hio_device_tests --gtest_filter='Session.*' --gtest_repeat=30` passed all 30 iterations (49 tests each) under linux-tsan and under linux-asan, no sanitizer report.

### Review fixes (independent review round 3, 2026-10-10)

Verified state before these findings (coordinator, clean builds, 2026-10-10): debug, ASan and TSan 317/317 tests, 0
sanitizer reports; `Session.*` ×30 green under TSan (with `tools/sanitizers/tsan.supp`) and ASan; tidy 0 findings;
qmllint, translations (0 unfinished), clang-format and LFS clean. The round-3 review found R1–R14 and R16–R18 correct,
R15 incomplete (finding 1) and R19 partly done (finding 11). Decided 2026-10-11: fix findings 1–9 and 11, document 10
(R20–R30 below; finding n = R(19+n)).

- [x] R20 (finding 1) `ProbeLock` registry atomicity: the held-serials mutex is held across `tryLock` + add and
  across `unlock` + remove (`probe_lock.cpp`), so a same-process acquire never sees "our PID, not held" for a lock
  that is being taken or released. Test `Lock.ConcurrentAcquireAndReleaseOfTheSameProbe` (6 threads × 300
  acquire/release of one SN: never two holders, the lock file never removed under its holder; fails without the fix).
- [x] R21 (finding 2) A probe that always reports 0 mV: `JLinkProbe::connect()` takes `healthyVoltageSeen` (the
  session's healthy reading ≥ `minTargetMillivolts` on this probe); a failed connect is `target.no_power` only when
  0 < VTref < minimum, or VTref = 0 after a healthy reading; otherwise `target.connect_failed`, so the auto-reconnect
  power gate (armed by a healthy reading or a reported `target.no_power`) stays off and retries go on. Consequence:
  an unpowered board whose probe reads exactly 0 mV on the very first connect is reported as `target.connect_failed`
  (tests that model "no power" before any healthy reading now use a low non-zero VTref, 150 mV). Tests
  `Probe.ConnectFailureWithZeroVoltsBeforeAHealthyReadingIsConnectFailed`,
  `Probe.ConnectFailureWithZeroVoltsAfterAHealthyReadingIsNoPower`,
  `Session.AlwaysZeroVoltProbeWithACableGlitchKeepsRetryingAndRecovers`,
  `Session.FirstAttemptWithZeroVoltsIsConnectFailedAndKeepsRetrying`; power loss after a healthy reading unchanged:
  `Session.AutoReconnectOnlyWhilePowered`, `Session.PowerLossAfterAHealthyReadingIsDisconnect`.
- [x] R22 (finding 3) The network-J-Link check (`isNetworkProbe`, a `listProbes(UsbAndIp)` scan) runs once per
  serial number and `JLinkProbe`, i.e. on the first failed open with `-n` in a session, and once per tool run
  (`jlink info`, `update-firmware`), never on every auto-reconnect retry. Tests
  `Probe.NetworkProbeCheckRunsOnlyOnTheFirstFailedOpen`, `Session.NetworkProbeCheckRunsOnceDuringRetries` (counter
  `ScriptedTarget::ipLists`).
- [x] R23 (finding 4) `chester app command` waits for the echoes up to lines × RTT write timeout + 500 ms, ending
  earlier on the worker's write warning or when the session leaves Connected (then `target.not_connected` if lines
  are missing); `target.rtt_write_timeout` only after the whole budget. The fake library gained
  `acceptWritesAfterMs` (writes accept 0 bytes until that long after the first write attempt). Test
  `FakeJLinkCli.CommandWrittenAfter1900MsSucceeds`.
- [x] R24 (finding 5) `update-firmware` opens the probe normally (`DisableAutoUpdateFW` before and after Open) and
  reads the status "before" there; only when it is not Current the probe is closed, opened in
  `OpenMode::FirmwareUpdate`, updated, closed, opened normally and verified (R11). "Up to date" therefore only
  comes from a Normal-open reading. Tests `FirmwareUpdate.StatusBeforeIsReadAfterANormalOpen` (exact order),
  `FirmwareUpdate.CurrentFirmwareIsNotUpdated` (one Normal open only), `FirmwareUpdate.OutdatedIsUpdatedAndVerified`
  (three opens, `DisableAutoUpdateFW` × 4) and the other R11 tests updated.
- [x] R25 (finding 6) `reset(halt = true)` reports Disconnected without an error and blocks auto-reconnect (like
  `suspend()`) until an explicit reconnect (F4), `resume()` or a reset without halt; while halted, `target.not_connected`
  has no "wait for the console to reconnect" suggestion. Tests `Session.HaltResetIsNotReattachedAutomatically`
  (no reattach for 300 ms = 5 reconnect intervals — a fixed wait, nothing observable runs while halted; then
  F4 attaches; the fixed wait was replaced by a worker-cycle count in R40), `Session.ResetWithoutHaltAfterAHaltResetReattaches`.
- [x] R26 (finding 7) Explicit console file: after a failed rotation (rename to `P.1` fails) the file is not rotated
  again in this session, one `io.write_failed` Warning (`ConsoleFileSink::takeWarning()`, forwarded by the worker),
  appending continues; beyond 2 × 100 MiB (`maxExplicitFileBytesAfterFailedRotation`) `flush()` fails, the session
  drops the file with one more Warning. Bound documented in Bounds. Test
  `Sink.FailedRotationOfTheExplicitFileWarnsOnceAndStaysBounded` (`P.1` is a non-empty directory; sparse file).
- [x] R27 (finding 8) Catch-alls in every CLI thread body: replxx input (`console_ui.cpp`, exception = end of input),
  `StdinLineReader` (`terminal.cpp`, exception = end-of-input marker) and the POSIX signal watcher (`signals.cpp`,
  exception = stop request); each logs a critical line (`logThreadException`, `cli_log.h`). `StdinLineReader` gained an
  injectable read function for tests. Tests `StdinLineReader.ExceptionInTheReaderThreadEndsTheInput`,
  `StdinLineReader.DeliversLinesAndTheEndOfInput`. The replxx and signal-watcher catch-alls have no unit test: replxx
  needs a terminal, and the watcher's body (atomic exchange, `request_stop`, `sleep_for`) has nothing that can be
  made to throw without a test-only hook in a process-wide signal handler.
- [x] R28 (finding 9) The `--json` console emits exactly one terminal `stopped` event: `ConsoleView::printStopped()`
  is skipped when the last connection event it printed was Stopped (with or without an error); the error envelope
  still follows a fatal error. Tests `JsonConsoleView.OneStoppedEventAfterAFatalError`,
  `JsonConsoleView.OneStoppedEventAfterANormalEnd` (in-process `ConsoleView` + fake library),
  `FakeJLinkCli.NoPowerIsTargetError` and `FakeJLinkCli.ConsoleJsonWithPipedStdin` (count = 1 in the NDJSON).
- [x] R29 (finding 10) documented, no code change: `BoundedQueue::push` (`DropOldestMatching`) and Bounds above —
  with no matching element the oldest element of any kind is dropped and counted in `droppedCount` (reported as
  dropped lines); practically unreachable.
- [x] R30 (finding 11) Wall-clock fragility removed from `FakeJLinkCli.CommandTimeoutCountsFromTheWrite` (the write
  is accepted 1.5 s after the first write attempt — `acceptWritesAfterMs` — instead of a stall relative to boot;
  timeout 1 s) and `FakeJLinkCli.ConsoleLingerCollectsLateOutput` (the late line is the reply to a piped command,
  1 s after its write — fake `responseDelayMs` — instead of 1.5 s after boot; `--linger 0` vs `4`, the echo is
  checked). Repeat runs (2026-10-11): `ctest --repeat until-fail:20` of both tests and of
  `FakeJLinkCli.CommandWrittenAfter1900MsSucceeds` passed 20/20 under linux-tsan and under linux-asan.

Verification of R20–R30 (2026-10-11, clean asan / tsan / tidy build directories): debug, ASan and TSan 333/333
ctest tests (the 5 `Hil.*` skipped without `HIO_HIL=1`), 0 sanitizer reports; `hio_device_tests
--gtest_filter='Session.*:Lock.*' --gtest_repeat=30` 30/30 (63 tests each) under TSan (with
`tools/sanitizers/tsan.supp`, no new suppression) and ASan; clang-tidy 0 findings; qmllint clean; translations up
to date, 0 unfinished Czech entries (no new user-visible strings); clang-format and Git LFS clean.

ABI cross-check (2026-10-11, pylink-square 2.0.2 and rttt 1.9.2 sdists re-downloaded, read only): no mismatch in
struct layouts, sizes, offsets, constants, function signatures or the Windows calling convention (`__stdcall`
only for the three `JLINK_RTTERMINAL_*` functions on 32-bit x86, as pylink's `windll` list). Fixed one comment in
`jlink_abi.h` (pylink's `MAX_BUF_SIZE` is 336, not 256; our 512-byte buffers are unaffected). Not checkable against
pylink: `JLINKARM_Open` (pylink uses `OpenEx`), `JLINKARM_SetLogOutHandler` (pylink uses `EnableLog`), and the
`char` return type of `IsOpen` / `IsConnected` / `Halt` (pylink reads `int`) — HIL.

### Review fixes (independent review round 4, 2026-10-11)

Verified state before these findings (coordinator, clean builds, 2026-10-11): debug, ASan and TSan 333/333, 0 sanitizer
reports; `Session.*:Lock.*` ×30 under TSan and ASan; the three timing-sensitive CLI e2e tests ×20 under TSan and ASan;
tidy 0 findings; qmllint, translations (0 unfinished), clang-format and LFS clean. Round 4 found R20–R27, R29 and R30
correct, and R28 incomplete (finding 4). Decided 2026-10-11: fix all 10 findings (R31–R40; finding n = R(30+n)) and
document the differences from rttt (Design → `libs/device`). Each test below fails on the code before its fix, except
R36 (a robustness rewrite of an existing test).

- [x] R31 (finding 1, medium) Retry pacing: `reconnect()` sets `m_nextReconnect = now + reconnect interval` after
  `connect()` returns, whatever the outcome (manual and automatic), so retries that fail like the previous one (and
  are therefore not reported again) are no longer run back to back. Test
  `Session.FailedRetriesArePacedFromTheEndOfEachAttempt` (every attach ends `target.rtt_not_found` after a 300 ms
  deadline, reconnect interval 300 ms; each `rttStart()` is ≥ one interval after the previous attempt's
  `rttStop()` — the old code started the next attempt 0 ms later — and a command sent during the retries is
  handled within one attempt plus one cycle). `ScriptedTarget` records `rttStartTimes` / `rttStopTimes`.
- [x] R32 (finding 2, medium-low) SIGPIPE: `SignalGuard` ignores SIGPIPE (POSIX; the previous action is restored by
  its destructor). `Output::outFailed()` reports a failed stdout write; `ConsoleView` calls its output-closed handler
  once, and the console turns it into a stop request on its own `std::stop_source` (Ctrl+C / SIGTERM are forwarded to
  it with a `std::stop_callback`), so the session closes normally, the console file gets its `@ stopped` trailer and
  is flushed, and the command ends with `cancelled` (exit 130, detail "the standard output was closed"). Exit code
  choice: `cancelled` — the reader ending the pipeline is the environment stopping the run, like Ctrl+C, and not a
  failure of the device or of a file (ADR-0024 `io` = 3 would make `hio … | head` look like an error); no new
  category, so ADR-0024 is unchanged. Tests `FakeJLinkCli.ConsoleJsonEndsWhenTheReaderCloses` (`bash -c '… |
  head -n 1'` with stdin left open and target output every 50 ms: exit 130, not 141, in < 20 s, the console file ends
  with ` @ stopped`), `JsonConsoleView.FailedStdoutWriteCallsTheOutputClosedHandler`.
- [x] R33 (finding 3, low-medium) Always-0 mV probe: a successful `hardwareStatus()` while no healthy reading has been
  seen counts as powered (`m_powered = true`, in the periodic check and right after a connect), so data reports
  Connected again after a status-read glitch. Test `Session.AlwaysZeroVoltProbeRecoversAfterAFailedStatusRead`.
- [x] R34 (finding 4, low) R28 completion: `waitForConsoleConnection()` (`console_ui.h`) runs `waitUntilConnected` /
  `waitForFirstAttempt`; on any failure (the session's error, `timeout`, `cancelled`) it closes the session and calls
  `ConsoleView::printStopped()`, so the `--json` console emits exactly one terminal `stopped` event before the error
  envelope. Tests `JsonConsoleView.OneStoppedEventAfterAConnectTimeout`,
  `JsonConsoleView.OneStoppedEventWhenCancelledWhileConnecting` (in-process, fake with `connectDelayMs` and no RTT
  control block), `FakeJLinkCli.CtrlCWhileConnectingEmitsOneStoppedEvent` (SIGINT during connecting: exit 130, one
  `stopped` event, then the `cancelled` envelope).
- [x] R35 (finding 5, low) The `target.not_connected` suggestion "Wait for the console to reconnect." only when
  auto-reconnect is on and the session is not halted, suspended or fatally stopped; otherwise the default "Reconnect
  the console (F4).". Tests `Session.NotConnectedSuggestionWhileHaltedIsReconnect`,
  `Session.NotConnectedSuggestionWhileSuspendedIsReconnect`, `Session.NotConnectedSuggestionAfterAFatalStopIsReconnect`,
  `Session.NotConnectedSuggestionWithoutAutoReconnectIsReconnect`; the auto-reconnect case stays
  `Session.SendWhileNotConnectedIsAWarning`.
- [x] R36 (finding 6, low) `FakeJLinkCli.CommandWrittenAfter1900MsSucceeds` replaced by
  `FakeJLinkCli.CommandLinesWrittenAfter1500MsEachSucceed`: two command lines, each accepted 1.5 s after its own first
  write attempt (new fake key `acceptEachWriteAfterMs`): 3 s in total, more than one line's echo budget (2.5 s) and
  within two lines' (4.5 s); every write ends 0.5 s before the worker's 2 s write timeout (was 0.1 s). Design choice:
  no injectable write timeout — the production code keeps no test-only hook (an environment variable honoured only in
  test builds would make the tested binary differ from the shipped one); the margin comes from the scenario instead.
- [x] R37 (finding 7, low) `ProbeLock::owner()` applies the stale rules of `acquire()`: QLockFile's (on this host the
  PID must be alive and run the program recorded in the lock file — `processNameByPid` as Qt: `/proc/<pid>/exe` on
  Linux, `proc_name` on macOS, `QueryFullProcessImageNameW` on Windows) and the own-PID rule (our PID but not in the
  held registry → no owner, R15). Tests `Lock.LiveProcessWithAnotherAppNameIsNoOwner` (POSIX; `acquire()` takes that
  lock over too), `Lock.OwnPidNotHeldIsNoOwner`.
- [x] R38 (finding 8, low) `defaultConnectTimeout` is derived: `connectTimeoutFor(ProbeTiming, RttTiming,
  ConnectTimeoutBudget)` = library load 5 s + `connectAttempts` × (one connect 3 s + retry delay) + attach deadline +
  logger wait (it can start just before the attach deadline) + margin 2 s = 35.5 s (`static_assert`; Bounds). Test
  `Session.ConnectTimeoutCoversSlowConnectsAndTheAttachDeadline` (shortened timings, the same formula: 100 ms library
  load, the 3rd of three 100 ms connects succeeds, no RTT → `target.rtt_not_found`, not `timeout`; the attempt takes
  longer than attach deadline + logger wait + retry delays alone).
- [x] R39 (finding 9, low) `chester app command` queues its lines while the event loop runs; `internal.queue_full`
  keeps the rest for the next round (as the console's `LineSender`). The echo budget counts per line as written: line
  n + 1 is awaited until start + (n + 1) × write timeout + 500 ms (n = lines written so far), at most lines × write
  timeout + 500 ms. Test `FakeJLinkCli.CommandWith300LinesSucceeds` (each write accepted at its second attempt, so the
  queue fills; 300 answers).
- [x] R40 (finding 10, low) `Session.HaltResetIsNotReattachedAutomatically` waits for worker cycles instead of 300 ms:
  test hook `OpenHooks::cycles` / `WorkerConfig::cycles` (a shared atomic counter incremented once per worker loop
  iteration; nullptr in production). The test waits for 3 × reconnect interval / latency cycles (36; each cycle
  lasts ≥ the 5 ms latency and checks auto-reconnect) while halted: no `rttStart`, still Disconnected, then F4
  attaches.

Verification of R31–R40 (2026-10-11, clean asan / tsan / tidy build directories): debug, ASan and TSan 348/348 ctest
tests (the 5 `Hil.*` skipped without `HIO_HIL=1`), 0 sanitizer reports; `hio_device_tests
--gtest_filter='Session.*:Lock.*' --gtest_repeat=30` 30/30 (72 tests each) under TSan (with
`tools/sanitizers/tsan.supp`, no new suppression) and ASan; `ctest --repeat until-fail:20` of the added / changed
CLI tests (`FakeJLinkCli.ConsoleJsonEndsWhenTheReaderCloses`, `.CtrlCWhileConnectingEmitsOneStoppedEvent`,
`.CommandLinesWrittenAfter1500MsEachSucceed`, `.CommandWith300LinesSucceeds`, `JsonConsoleView.*`) 9 × 20 passed under
TSan and ASan; clang-tidy 0 findings; qmllint clean; translations up to date, 0 unfinished Czech entries (no new
user-visible strings); clang-format and Git LFS clean.
Environment finding: on the dev machine `DEBUGINFOD_URLS` points to debuginfod.ubuntu.com and the network is not
reachable from the sandbox; when TSan symbolizes a report to match it against a suppression (the documented Qt
`QMetaObject::invokeMethod` entry, hit most often by the many wake-ups of `CommandWith300LinesSucceeds`),
llvm-symbolizer blocks on that lookup and the test process is killed after 60 s. The TSan runs above therefore ran
with `DEBUGINFOD_URLS=` (empty; local symbolization only, no check weakened). Setting it in the sanitizer test
presets is left to a decision of the user (decided 2026-10-11: set empty, T1). `CliProcess::run` now keeps the output of a process it kills (diagnosis).

### Review fixes (independent review round 5, 2026-10-11)

Verified state (coordinator, clean builds, 2026-10-11): debug and ASan 348/348; TSan 348/348 only with
`DEBUGINFOD_URLS=` empty. With this machine's default `DEBUGINFOD_URLS`, two e2e tests were killed after 60 s
(`CommandLinesWrittenAfter1500MsEachSucceed`, `CommandWith300LinesSucceeds`): llvm-symbolizer blocks on an unreachable
debuginfod server while symbolizing the already-suppressed Qt race. `Session.*:Lock.*` ×30 and the 9 changed e2e tests
×20 pass under TSan and ASan; 0 sanitizer reports; tidy 0; qmllint, translations, clang-format and LFS clean.
Round 5 found R31–R36, R39 and R40 correct. R37 is broken on Windows (finding 1), and R38's test can't fail (finding 3).

Decided 2026-10-11: fix all 7 findings (R41–R47; finding n = R(40+n)) and set `DEBUGINFOD_URLS` empty in the sanitizer
test presets (T1). Each test below fails on the code before its fix, except where noted.

- [x] R41 (finding 1, medium) `ProbeLock::owner()` follows QLockFile exactly. Qt source read: qtbase **v6.8.3** (our
  pin, CI `QT_VERSION`), `src/corelib/io/qlockfile.cpp`, `qlockfile_unix.cpp`, `qlockfile_win.cpp`,
  `kernel/qcore_unix_p.h`. QLockFile has no public non-acquiring stale check (`isApparentlyStale()`,
  `isProcessRunning()`, `processNameByPid()` are private; `tryLock()` takes a stale lock over, which a listing must not
  do because a concurrent `acquire()` of another process would then fail), so the rules are mirrored per OS in
  `probe_lock.cpp`, citing the Qt lines; `acquire()` keeps using QLockFile itself. Qt's rules (staleLockTime 0, as ours):
  the lock file holds PID, app name, host name, machine id, boot id (`lockFileContents()`). Recorded app name =
  `processNameByPid(own PID)`: Windows the module file name without directory and extension (`hio`), Linux the file
  name of `/proc/<pid>/exe` without " (deleted)", macOS `proc_name()`. Recorded host name = `machineName()`: Windows
  `%COMPUTERNAME%`, else `QSysInfo::machineHostName()`. Stale (`isApparentlyStale()`): same host = the machine id when
  both sides know it, else an empty or equal host name; on the same host, another boot id or an owner that does not run
  (Windows: `OpenProcess(PROCESS_QUERY_INFORMATION)` fails — also access denied — or the exit code is not
  `STILL_ACTIVE`, or the name differs, compared as is; POSIX: `kill(pid, 0)` fails with `ESRCH`, or the name differs
  from the recorded one's file name after resolving a symlink; Linux without procfs or macOS without a name: no name
  comparison). A lock of another host is never stale. `tryLock()` takes an apparently stale lock only when
  `removeStaleLock()` succeeds: on Windows `QFile::remove()` fails while the owner keeps the file open (no
  `FILE_SHARE_DELETE`), on POSIX `flock(LOCK_EX | LOCK_NB)` fails while the owner holds its flock. `owner()` checks that
  without removing anything (Windows: open for `DELETE` with every share mode; POSIX: open for writing, `flock`,
  close). A lock file that cannot be read is not stale for Qt (`acquire()` fails with an unknown owner), so `owner()`
  reports `?` / PID 0. The R15 own-PID rule is kept. `ProbeLockOwner` gained `lockedAt` (the lock file's time, for R45).
  Tests (Linux and all OSes): `Lock.LockOfAnotherProcessThroughQLockFileHasAnOwner` (a real QLockFile taken by this
  test binary in a helper mode, `HIO_TEST_HOLD_LOCK_FILE`, `libs/device/tests/lock_holder.h`; runs in Windows CI too, where the old
  code found no owner), `Lock.UnreadableLockFileIsAnUnknownOwner`, POSIX `Lock.LockOfAnotherBootIsNoOwner`,
  `Lock.MachineIdDecidesTheHost` (skipped without a machine id), `Lock.ApparentlyStaleLockStillHeldByItsOwnerHasAnOwner`;
  Windows only (compiled under `_WIN32`, run by Windows CI): `Lock.WindowsRecordsTheAppNameWithoutExtensionAndTheComputerName`,
  `Lock.WindowsComparesTheAppNameWithoutExtension`. G2 gained the manual check "`jlink list` shows in use on Windows".
- [x] R42 (finding 2, low-medium) `chester app command` response bounded (`CommandResponse`,
  `app/cli/src/command_response.h`): at most 10 000 lines (output + log) and 4 MiB of text; beyond the bound the first
  lines are kept and nothing more is collected, the collection ends early, `--json` `data.truncated` is `true` (`false`
  otherwise) and human mode prints "Output truncated after N lines" on stderr. Bounds and the CLI table updated. Tests
  `FakeJLinkCli.CommandOutputIsTruncatedAfter10000Lines` (a 12 000-line reply, `--timeout 30` ends early),
  `CommandResponse.KeepsTheFirst10000LinesOfOutputAndLog`, `CommandResponse.KeepsAtMost4MiBOfText`,
  `CommandResponse.UntruncatedByDefault`, `FakeJLinkCli.CommandVersionJson` (`truncated: false`).
- [x] R43 (finding 3, low) `connectTimeoutFor()` tests: `ConnectTimeout.HandComputedValues` (production 35.5 s;
  distinct powers of ten per term, 111 331 ms) and one scenario per term in which that term dominates; each ends with
  its own result, not `timeout`, and takes longer than the timeout would be without the term, and each checks the
  hand-computed timeout of its test timings: `ConnectTimeout.LibraryLoadTermCoversASlowLoad`,
  `.ConnectAttemptTermCoversThreeSlowConnects`, `.RetryDelayTermCoversTheDelaysBetweenConnects`,
  `.AttachDeadlineTermCoversFirmwareWithoutRtt`, `.LoggerWaitTermCoversALateLogger` (terminal found, logger never:
  Connected after the logger wait), `.MarginCoversOtherDelays` (a slow probe open). Replaces
  `Session.ConnectTimeoutCoversSlowConnectsAndTheAttachDeadline`. Checked by mutation: dropping any one term (or the
  × attempts) makes the dominating test time out and every hand-computed check fail.
- [x] R44 (finding 4, low) A closed stdout keeps `cancelled` (exit 130) with its own translatable message "The output
  was closed." (`hio::outputClosedError()`, `libs/core`, context `hio::core`; Czech "Výstup byl uzavřen."); the detail
  "the standard output was closed" is unchanged. It is detected on the next write to stdout after the reader went away
  (CLI table). Tests `MakeError.OutputClosedIsCancelledWithItsOwnMessage`,
  `FakeJLinkCli.ConsoleReportsTheClosedOutputWithItsOwnMessage` (human mode `| head -n 1`, stderr).
- [x] R45 (finding 5, low) Console-file retention never deletes a file a live session may be writing: files this
  process has open (a process-wide registry of open sink files), and files whose probe SN (file name) has a live
  owner per `ProbeLock::owner()` (R37/R41 rules, this or another process; `ConsoleFileConfig::lockDirectory`, the
  registry's lock directory). **Refinement (not in the decision text, to be confirmed by the user):** only the files of
  that SN written since the owner took its lock (the lock file's time, 2 s tolerance) count as live — a session always
  holds its own probe's lock while it prunes, so without this a single-probe user's older files would never be pruned.
  Live files count towards the bounds, which may therefore be exceeded temporarily (Bounds). Tests
  `Sink.RetentionKeepsTheFilesOfALiveProbeLock` (the oldest file, of the locked SN X, is kept; an X file from before the
  lock and the other old files beyond 50 are pruned; after release it goes), `Sink.RetentionKeepsTheFilesOfAProbeLockedByAnotherProcess`
  (POSIX), `Sink.RetentionKeepsTheFilesThisProcessHasOpen`. Corrected by R48 (round 6): only the file a live session
  is writing counts as live; the first two tests were replaced.
- [x] R46 (finding 6, low) Wall-clock thresholds at about half of the delay they distinguish:
  `History.SaveAttemptsStayAtEvery50AddsWhenSavesAreSkipped` < 490 ms (49 × 20 ms lock waits = 980 ms),
  `Session.OpenReturnsWithoutTouchingTheLibrary` < 500 ms (1000 ms library load),
  `FakeLibrarySession.OpenDoesNotWaitForASlowConnect` < 500 ms (1000 ms connect). Robustness change, no failing-before
  test.
- [x] R47 (finding 7, low) `connectTimeoutFor()` is not scaled with `--jlink-speed`: Bounds and Risks state that the
  budget assumes about 1000 kHz or more and very low speeds may end with `timeout`; G1 measures connect and attach at
  5, 100 and 1000 kHz. Documentation only. Since ADR-0090 (R49) the caveat applies only to an explicit low kHz value;
  the default is the automatic speed.
- [x] T1 (tooling) The sanitizer test presets (`linux-asan`, `linux-tsan`, `macos-asan`; the only ones with sanitizers)
  set `"DEBUGINFOD_URLS": ""`, so llvm-symbolizer uses local debug info only and never blocks on an unreachable
  debuginfod server (schema 6 presets cannot hold comments; the reason is noted in `tools/sanitizers/tsan.supp` and
  under Tests and verification). TSan reports still symbolize from the local debug info.

References: rttt 1.9.2, pylink-square 2.0.2 and the Qt 6.8.3 QLockFile sources are kept read-only in
`/home/hardwario/src/monitor/refs/` (outside the repositories, decided 2026-10-11); round 5 itself could not compare
against rttt because its scratchpad copies had disappeared.

Verification of R41–R47 and T1 (2026-10-11, clean asan / tsan / tidy build directories, `DEBUGINFOD_URLS` left as on
the machine — the presets set it): debug, ASan and TSan 368/368 ctest tests (the 5 `Hil.*` skipped without
`HIO_HIL=1`), 0 sanitizer reports; `ctest --repeat until-fail:20` of `FakeJLinkCli.CommandOutputIsTruncatedAfter10000Lines`,
`.ConsoleReportsTheClosedOutputWithItsOwnMessage`, `.CommandVersionJson`, `.CommandLinesWrittenAfter1500MsEachSucceed`
and `.CommandWith300LinesSucceeds` 5 × 20 passed under TSan and ASan; `hio_device_tests
--gtest_filter='Session.*:Lock.*:Sink.*:ConnectTimeout.*' --gtest_repeat=30` 30/30 (93 tests each) under TSan (preset
`TSAN_OPTIONS`, `DEBUGINFOD_URLS=`; no new suppression); a TSan run with `print_suppressions=1` matched the documented
Qt entry 157 times, i.e. reports are symbolized from local debug info; clang-tidy 0 findings; qmllint clean;
translations up to date, 0 unfinished Czech entries (one new string); clang-format and Git LFS clean. Windows-only
tests compile only under `_WIN32` and were not built here (Windows CI).

### Review fixes (independent review round 6, 2026-10-11)

Verified state (coordinator, clean builds, default environment incl. `DEBUGINFOD_URLS`, 2026-10-11): debug, ASan and
TSan 368/368, 0 sanitizer reports. T1 works: no hangs without unsetting debuginfod. `Session.*:Lock.*:Sink.*:ConnectTimeout.*`
×30 under TSan and ASan; 7 e2e tests ×20 under TSan and ASan; tidy 0; qmllint, translations, clang-format and LFS clean.
Round 6 (with the rttt, pylink and Qt 6.8.3 QLockFile sources) found R41–R44, R46, R47 and T1 correct, and R41
matching Qt line by line. R45 is wrong in one respect (finding 1).

Decided 2026-10-11: fix all 9 findings (R48–R56; finding n = R(47+n)) and correct the Differences-from-rttt list
(connect speed: auto like pylink, ADR-0090). Each code fix has a test that fails on the code before it (checked by
reverting the fix), except R56 (the old tests pass on this machine's coreutils `sleep`).

- [x] R48 (finding 1, medium) R45 regression: only a file a live session is writing counts as live. In this process:
  the open-files list (a lock held by this process no longer protects anything by itself). In another process: only
  the newest file of that SN (by modification time, `.partN` included) written since the owner's `lockedAt` (2 s
  tolerance). Closed `.partN` files of a live session and older files are pruned; the bounds (50 files / 200 MiB) hold
  apart from at most one live file per live session (Design, Bounds, `console_file_sink.h`). Tests
  `Sink.RetentionPrunesTheClosedPartsOfALiveSession` (this process holds the lock; the session rotated into
  `.part3`; its closed parts, written after the lock was taken, are pruned, the current part is kept) and POSIX
  `Sink.RetentionKeepsOnlyTheNewestFileOfAProbeLockedByAnotherProcess` (a real QLockFile in a helper process; its
  `.part2` is kept beyond the bound, its closed first part and an older session's file are pruned; after release it
  goes); they replace `Sink.RetentionKeepsTheFilesOfALiveProbeLock` and
  `Sink.RetentionKeepsTheFilesOfAProbeLockedByAnotherProcess`. `Sink.RetentionKeepsTheFilesThisProcessHasOpen`
  unchanged.
- [x] R49 (finding 2, medium) Connect speed (decided 2026-10-11: auto, like pylink; ADR-0090). pylink-square 2.0.2
  `JLink.connect(chip, speed='auto')` sends `Device = <chip>`, then `set_speed(auto=True)`: `speed = 0`,
  `speed | AUTO_JTAG_SPEED` (`0x0`), `JLINKARM_SetSpeed(0)`, then `JLINKARM_Connect` (only when not connected yet).
  Mirrored through the existing `IJLinkApi::setSpeed()` with `jlink::autoSpeed` = 0 (no new library call; source
  cited in `jlink_api.h`). `JLinkProbe::setSpeed(std::optional<int>)` (nullopt = auto) applies the
  speed at once (before the SWD selection, as before) and `connect()` applies it again right after `Device =`, before
  `JLINKARM_Connect`. `TargetProfile::defaultSpeedKhz` and `SessionOptions::speedKhz` are `std::optional<int>`
  (nullopt = auto; CHESTER: nullopt). CLI `--jlink-speed auto|KHZ` (default `auto`; a kHz value 5–50 000; anything else
  `usage.invalid_argument` when the arguments are parsed, `parseJLinkSpeed()` in `chester_commands.h`); the console
  prints "Connecting to NRF52840_xxAA at auto speed..." (or "at N kHz"). `hio jlink info` and `update-firmware` connect
  no target and set no speed, so nothing changes there. Tests `Probe.ConnectSetsTheAutomaticSpeedAfterTheDeviceAsPylink`
  (GMock order `Device = …` → `setSpeed(0)` → `connect()`), `Probe.AutomaticSpeedIsSetAsZero`,
  `Probe.ExplicitSpeedIsFixedForTheConnect` (4000 at once and after `Device =`, never 0),
  `Session.SpeedIsAutomaticByDefaultAndFixedWhenGiven` (`ScriptedTarget::speeds`), `CliApp.JLinkSpeedAcceptsAutoOrKiloHertz`,
  `CliApp.InvalidJLinkSpeedIsUsageError` (`fast`, `1`, `4`, `50001`, an empty value, `2000k`, `-5`, `auto2`),
  `ParseJLinkSpeed.AutoIsNulloptAndKiloHertzIsFixed`, `FakeJLinkCli.InvalidSpeedIsUsageError` (`1` and `fast` → exit 2),
  `Profiles.ChesterAppTarget` (default nullopt).
- [x] R50 (finding 3, low-medium) The decision row and the CLI table describe "`--auto-reconnect` and the first
  connect" as our own rule (rttt and py-hardwario end on a probe-open or target-connect failure even with
  `--auto-reconnect`; only the RTT attach of rttt's `connector.open()` retries); listed in Differences from rttt. No
  code change.
- [x] R51 (finding 4, low) `ProbeLock::acquire()`: when both `tryLock()` attempts fail and the lock read in the second
  looks stale (QLockFile's rules as `owner()` mirrors them — dead PID, another boot, another program — or the file is
  gone, or our own PID that we do not hold), it waits 10 ms and tries once more before reporting
  `probe.in_use_by_other_process` (bounded: one extra attempt, under the held-serials mutex). Covers `owner()` of
  another process holding the stale file's `flock` (POSIX) or opening it without `FILE_SHARE_DELETE` (Windows) for a
  moment. Test POSIX `Lock.StaleLockBrieflyHeldByAnOwnerCheckIsTakenOver` (a thread holds a `flock` on a stale lock
  file of a dead process for 5 ms through its own file description; the old code reported the dead owner).
- [x] R52 (finding 5, low) A failed `JLINKARM_GetHWStatus` of the open probe is the new code `probe.io_failed` (Probe,
  exit 5; "Communication with the J-Link failed." / the J-Link did not answer: a USB glitch, or the USB cable was
  disconnected / "Check the USB cable between the J-Link and the computer; the console reconnects automatically if
  enabled."; Troubleshooting; Czech texts). `target.rtt_read_failed` was not used: its texts speak of reading from the
  device and of the flat cable, while this is the J-Link itself not answering. Callers: the session's power check
  reports Disconnected with `probe.io_failed` (the auto-reconnect retry still closes and reopens the probe, review
  fix 2; the gate ignores the code); `chester app command` ends with that error (exit 5 as before, no longer "No
  J-Link probe found."); `jlink info` ignores a failed read (`targetMillivolts` null, unchanged); `JLinkProbe::connect()`
  reads the status through the API itself (cable diagnosis, unchanged). Tests `MakeError.ProbeIoFailedCode`,
  `Probe.HardwareStatusFailureIsProbeIoFailed` (replaces `Probe.HardwareStatusFailureMeansTheProbeIsGone`),
  `Session.FailedStatusReadIsProbeIoFailed`.
- [x] R53 (finding 6, low) An empty or unreadable lock file (owner `?`, PID 0) keeps Qt's semantics (not stale, never
  deleted automatically); its `probe.in_use_by_other_process` error gets the suggestion "If no other HARDWARIO program
  is running, delete <path>." (translatable, context `hio::device`, native separators; Czech). The detail names the
  lock file for every owner. Test `Lock.UnknownOwnerSuggestsDeletingTheLockFile`.
- [x] R54 (finding 7, low) The event queue drops only console output lines (`Direction::Out`); echoes
  (`Direction::In`) are excluded from the `DropOldestMatching` predicate (`isDroppableEvent`, `session_internal.h`),
  so `command` counts every written line. Bounded by the command queue; Bounds and R10 updated. Test
  `Session.EventOverflowKeepsEveryEcho` (40 commands answered by 500 lines each while the owner does not drain: lines
  are dropped, all 40 echoes arrive in order; the old code delivered 8).
- [x] R55 (finding 8, low) CLI table: the `command` timeout is counted from the last echo (written line); the lines are
  written before that within at most lines × write timeout + 500 ms (R7, R23, R39). Documentation only.
- [x] R56 (finding 9, low) The lock tests record the name Qt's `processNameByPid()` reports for the `sleep` helper
  (`runningAppName()`: the file name of `/proc/<pid>/exe` on Linux, `proc_name()` on macOS; read right after the helper
  started) instead of "sleep", so busybox and multicall coreutils images pass: `Lock.HeldByAnotherProcessNamesTheOwner`,
  `Lock.StaleLockOfADeadProcessIsTakenOver`, `Lock.LiveProcessWithAnotherAppNameIsNoOwner`,
  `Lock.LockOfAnotherBootIsNoOwner`, `Lock.MachineIdDecidesTheHost`, and the new
  `Lock.StaleLockBrieflyHeldByAnOwnerCheckIsTakenOver`. No failing-before test here (this machine's `sleep` is its own
  binary).
- [x] Differences from rttt (Design → `libs/device`) corrected as the review asked, each statement checked against the
  rttt 1.9.2 and pylink 2.0.2 sources: halt reset (rttt re-attaches only with `--auto-reconnect`), missing logger (only
  an unregistered name fails after 15 s in rttt), and added connect speed, the first connect with
  `--auto-reconnect`, rttt's 1 s wait after `--reset` (listed, behaviour not changed), the down buffer by name, state
  de-duplication by error code and the `connecting` state, a write while detached, and the silent probe firmware update
  of pylink's `disable_dialog_boxes()`. The 2 s write timeout and the stale-control-block retry were not in the list;
  they match rttt (noted under the list).

Verification of R48–R56 (2026-10-11/12, clean asan / tsan / tidy build directories, default environment): debug, ASan
and TSan 380/380 ctest tests (the 5 `Hil.*` skipped without `HIO_HIL=1`), 0 sanitizer reports; `ctest --repeat
until-fail:20` of the 39 added or changed tests (`MakeError.ProbeIoFailedCode`, the R49 / R52 `Probe.*` tests,
`Session.SpeedIsAutomaticByDefaultAndFixedWhenGiven`, `.FailedStatusReadIsProbeIoFailed`, `.EventOverflowKeepsEveryEcho`,
`Sink.Retention*`, `Lock.*`, the speed `CliApp.*` / `ParseJLinkSpeed.*` tests, `FakeJLinkCli.InvalidSpeedIsUsageError`,
`Profiles.*`) passed 20/20 under TSan and ASan; `hio_device_tests --gtest_filter='Session.*:Lock.*:Sink.*:ConnectTimeout.*'
--gtest_repeat=30` 30/30 (98 tests each) under TSan (preset `TSAN_OPTIONS`, `DEBUGINFOD_URLS=`; no new suppression);
clang-tidy 0 findings; qmllint clean; translations up to date, 0 unfinished Czech entries (4 new strings); clang-format
and Git LFS clean.

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
- **Very low explicit SWD speeds** (R47): the 35.5 s connect timeout assumes about 1000 kHz or more; at an explicit
  `--jlink-speed` of e.g. 5 kHz the connect and attach may take longer and end with `timeout` instead of the session's
  own error. The default automatic speed (ADR-0090) is not concerned. Not scaled yet; HIL (G1) measures auto and 5,
  100 and 1000 kHz.
- Found during implementation (2026-10-09): replxx master is only 15 commits ahead of `release-0.0.4` and has had no
  commit since 2021-11-25 (ADR-0064 assumed "years of fixes"); the dependency is effectively unmaintained. It builds
  with one third-party compiler warning (`-Wcharacter-conversion` in `terminal.cxx`, not our flags). The other
  open points of the implementation were decided on 2026-10-09 (see the decision table).
