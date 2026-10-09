# ADR-0058 — Single GUI instance and cross-process probe and file locks

- **Status:** Accepted (amends feature 0002 and ADR-0030)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 planning review)

## Context

`SessionRegistry` (0002) enforces "one session per probe SN" only inside one process. Two processes can
interfere:
- a second GUI;
- the GUI and the CLI;
- two CLIs.

Each loads its own J-Link library copy (ADR-0030), so the second process can open a probe that a GUI tab is using.
The result is an unclear `probe.open_failed`, or both processes fighting over the probe. All processes also share
the settings file (ADR-0025) and the command-history files (0002). The old app had an application lock and later
removed it (commit 1132416).

## Options considered

1. **Single GUI instance plus cross-process probe locks in the backend (chosen).**
2. Single GUI instance only. A GUI/CLI clash on a probe would surface as `probe.open_failed`.
3. Allow any number of instances with no coordination (last writer wins).

## Decision

- **Single GUI instance:**
  - On start, `hio-monitor` takes `QLockFile <AppLocalData>/gui.lock` (stale-lock detection on, 0 wait).
  - If the lock is held, it connects to the `QLocalServer` named `hio-monitor-<user>` and sends `activate`. The
    first instance raises and focuses its window. The second instance exits with code 0.
  - If the server cannot be reached within 2 s (hung instance), the second instance shows a dialog: "HARDWARIO
    Monitor is already running but does not respond." [Quit] [Start anyway] (ignores the lock).
  - The CLI is never single-instance.
- **Probe locks (backend, M1, `libs/device`):**
  - `SessionRegistry::acquire(sn)` takes `QLockFile <AppLocalData>/locks/probe-<sn>.lock` after the in-process
    check.
  - The lock is held for the whole session lifetime, by the GUI and the CLI alike, and released in `close()` /
    the destructor (RAII `ProbeLock`).
  - If the lock is held by a live process → new error `probe.in_use_by_other_process` (category Probe, exit 5):
    "The J-Link %1 is used by another program (%2, PID %3)." / another HARDWARIO Monitor window or `hio` command /
    "Close the other program, or choose another J-Link." The app name and PID come from `QLockFile::getLockInfo`.
  - Stale locks (dead PID) are removed by `QLockFile`.
  - A probe without a known SN (open of the "first probe") locks after the SN is known. A lost race →
    `probe.in_use_by_other_process`, and the probe is closed again.
  - `ProbeEnumerator` reports `std::optional<ProbeLockOwner> inUseBy {appName, pid}` per probe. `hio jlink list`
    shows "in use (hio, PID 1234)" and `--json` adds `"inUseBy": {"app":"hio","pid":1234}` or `null`.
  - The GUI picker shows "In use by another program" (versus "In use (tab 2)").
  - Programs that do not use these locks (JLinkExe, nRF Util, the old Monitor) are not detected; they still end in
    `probe.open_failed`.
- **Shared files:**
  - Settings: `QSettings` already serialises INI writes with its own lock file. Writers call `sync()` after each
    change. Readers do not cache across long operations.
  - Command history: `CommandHistory::save()` takes `QLockFile <history>.lock` (wait ≤ 2 s), re-reads the file,
    merges the new entries in time order, applies the 1000-entry bound and writes atomically (`QSaveFile`). A lock
    timeout skips that save and logs a warning; the next save retries.
  - Console log files are per session and unique by name (timestamp + SN). They need no lock.

## Consequences

- A clear message instead of a vendor-level failure when the same probe is opened twice by our programs.
- Feature 0002 gains `ProbeLock`, the new error code, the `inUseBy` field and history merging. Tests need two
  processes (a CLI e2e test holding a probe while a second `hio` call fails with exit 5).
- Feature 0004 (M3 shell) owns the single-instance logic in `app/gui`.
- A later multi-window or multi-instance mode needs a new ADR (open question 17 covers detached tabs).
