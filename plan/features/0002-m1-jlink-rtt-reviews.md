# 0002 — M1 J-Link & RTT: review fixes (R1–R56)

Companion to [0002-m1-jlink-rtt.md](0002-m1-jlink-rtt.md), split off on 2026-10-16 to keep the plan under the
100 KB Git LFS limit (CLAUDE.md). It holds the six review rounds of the implementation, unchanged; they are part of
plan 0002's implementation steps and checklist.

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
  Superseded 2026-10-16 by ADR-0110 (HIL G1, H2): `rttAttachDeadline()` (`rtt_channel.h`) scales the attach deadline
  with a fixed speed below 1000 kHz (at most 60 s); `DeviceSession::open()` applies it and the CLI waits
  `connectTimeoutForSpeed()`; below 125 kHz the CLI warns (`slowSpeedWarning()`) and the `target.rtt_not_found`
  detail names the speed. Tests `RttAttachDeadline.*`, `Rtt.RttNotFoundDetailNamesTheSpeed`,
  `Session.AttachDeadlineScalesWithAFixedLowSpeed`, `SlowSpeedWarning.OnlyBelow125kHz`,
  `ConnectTimeout.HandComputedValues`, `FakeJLinkCli.SlowSpeedIsWarnedBeforeConnecting`.
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
