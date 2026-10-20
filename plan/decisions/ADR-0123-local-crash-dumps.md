# ADR-0123 — Crash dumps are written locally by an own minimal handler and never uploaded

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M7 planning session)

## Context

Open question 3. M0 has only a terminate handler that flushes the log (ADR-0020). Crash reports help support, but
uploads need consent, a server and a privacy policy.

## Options considered

1. **Own minimal handler, local files only (chosen).**
2. sentry-native (Crashpad backend) with uploads off: real minidumps on all OSes, but a large dependency and an
   extra handler executable to ship and sign.
3. Only OS facilities (WER, macOS DiagnosticReports, systemd-coredump).
4. Opt-in upload service.

## Decision

- New module in `libs/core` (`crash_handler`), installed first thing in `main()` of the GUI and the CLI, writing to
  `<AppLocalDataLocation>/crashes/`:
  - **Windows:** `SetUnhandledExceptionFilter` + `MiniDumpWriteDump` (`MiniDumpNormal | MiniDumpWithThreadInfo`),
    file `crash-<app>-<UTC>.dmp`; the vendor-style Win32 calls live in one source file, wrapped as the code rules
    require.
  - **Linux/macOS:** `sigaction` handlers for SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT on an alternate signal stack;
    the handler is async-signal-safe (pre-opened file descriptor, `backtrace` + `backtrace_symbols_fd`, `write`
    only), writes `crash-<app>-<UTC>.txt` with signal, address, app version and git hash and the raw backtrace,
    then restores the default action and re-raises (so the OS crash reporter / core dump still runs).
- **Bounds:** at most **5** crash files kept (oldest deleted at startup), each ≤ 8 MiB (minidumps) / 64 KiB (text).
- The diagnostic report (ADR-0075) adds the newest **3** crash files and, on macOS, the newest 3
  `~/Library/Logs/DiagnosticReports/hio*` files; redaction rules apply to text files. After a crash, the next GUI
  start shows a non-modal notice "HARDWARIO Monitor quit unexpectedly last time" with [Create diagnostic report].
- Nothing is uploaded. Symbol files (`.pdb`, `.dSYM`, split DWARF) of each release are attached to the GitHub
  release as a separate archive so support can symbolize dumps.

## Consequences

- No new dependency; dumps are less complete than Crashpad's on Linux/macOS (backtrace only).
- The handler is unit-tested in a child process that crashes on purpose; sanitizer builds disable it (they install
  their own handlers).
