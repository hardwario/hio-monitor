# ADR-0073 — Detect programs that hold the probe by scanning running processes

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M4 planning session)

## Context

Programs that do not use our probe locks (ADR-0058) — J-Link Commander, GDB server, nRF Connect, nRF Util,
debuggers, the old HARDWARIO Monitor — make `probe.open_failed` happen. The user often does not know which one.

## Options considered

1. Ask the user to close typical programs, without detection.
2. **Scan the running processes against a list of known holders (chosen).**

## Decision

- `libs/diag` gets `[[nodiscard]] Result<std::vector<ProcessMatch>> findProbeHolders()`; `ProcessMatch`
  `{ std::string displayName; std::string executable; qint64 pid; }`.
- The known-holder list is compiled in (executable name patterns, case-insensitive, with a display name):
  `JLinkExe`/`JLink.exe` (J-Link Commander), `JLinkGDBServer*`, `JLinkRTTViewer*`, `JLinkRTTLogger*`,
  `JFlash*`, `Ozone*`, `nrfutil*`, `nrfjprog*`, `nRF Connect*` / `nrfconnect*`, `hiomon*` / old
  `HARDWARIO Monitor` (v1), `pyocd`, `openocd`. Our own processes are excluded (they use locks).
- Per-OS code in `libs/diag/src/platform/`:
  - Linux: `/proc/<pid>/comm` and `/proc/<pid>/exe` (readlink);
  - macOS: `proc_listallpids` + `proc_pidpath` (libproc);
  - Windows: `CreateToolhelp32Snapshot` + `Process32FirstW/NextW`.
  No `reinterpret_cast` is needed; buffers are `std::array` / `std::vector`.
- Bounds: at most 4096 processes inspected, 2 s overall timeout (the check is then Warning "incomplete").
- Used by `hio doctor` (check `probe-holders`, Warning when a holder runs) and by the troubleshooting topics for
  `probe.open_failed` and `probe.not_found`.
- The result is a hint ("J-Link Commander (PID 4242) may be using the probe"); the app never kills processes.

## Consequences

- OS-specific code to maintain, covered by unit tests with a fake process source and a manual test per OS.
- The list must be updated when new tools appear (a table row).
