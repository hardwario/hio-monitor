# ADR-0030 — Several J-Link probes per process via per-session library copies

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M1 planning session)

## Context

One loaded instance of the JLinkARM library drives one probe connection (the library keeps global
state). Users may want to work with two devices at once (e.g. two CHESTERs in the GUI). pylink solves
this by copying the library to a temporary file for every `JLink` object, so the OS loads an independent
instance.

## Options considered

1. One active session per process in v2.0; a second device needs a second app instance.
2. Several sessions per process, each loading its own copy of the library (chosen).

## Decision

- Each `DeviceSession` loads **its own copy** of the JLinkARM library: the located library is copied into
  a per-session `QTemporaryDir` under `AppLocalDataLocation/jlink-instances/` and loaded from there
  (`QLibrary`, local symbol binding / `LoadLibrary`). The copy is unloaded and deleted when the session ends.
- Probe enumeration (`EMU_GetList`) uses a separate, short-lived instance.
- **Bounds:** at most **8** concurrent sessions per process (`internal.session_limit`); at most one session
  per probe serial number. At startup, instance directories older than 24 hours (left by crashes) are removed;
  deletion failures are ignored.
- Each session remains owned by exactly one worker thread (ADR-0031); instances are never shared between sessions.

## Consequences

- Multi-device work in one GUI window and one CLI process becomes possible; APIs are per session.
- Disk usage: one library copy per session (tens of MB), temporary.
- Risks verified by hardware tests: antivirus scanning/locking on Windows, macOS code signature of the copy
  (a byte copy keeps the signature), USB access from several instances at once.
- If copies prove unreliable on a platform, fall back to one session per process there via a new ADR.
