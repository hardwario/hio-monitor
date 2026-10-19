# ADR-0128 — BLE line assembly and console file on the BLE thread; file sink keyed by session

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad

Amends ADR-0080 ("no file I/O on the BLE thread — the console file sink runs on the owner thread") and plan 0011
("`LineAssembler` … on the owner thread"). Amends plan 0002 (`ConsoleFileSink` keyed by the probe serial number).

## Context

For J-Link sessions the worker thread assembles lines and writes the console file (`ConsoleFileSink`, flushed every
worker cycle); the owner thread only receives finished `ConsoleLine` batches. Plan 0011 put line assembly and the
file sink of BLE sessions on the owner thread to keep the shared BLE thread light. On the GUI that means file
writes (including rotation and retention pruning) on the GUI thread and a separate flush timer. `ConsoleFileSink`
also names files and checks retention by the J-Link serial number only (`ProbeLock::owner(sn)`), so BLE files would
never count as live in another process, and a BLE address contains `:`, which Windows does not allow in file names.

## Options considered

1. **Line assembly + file sink on the BLE thread, like the J-Link worker (chosen).**
2. On the owner thread with a periodic flush timer (plan 0011 as written).

## Decision

- Per BLE link, the BLE thread owns a `LineAssembler` (profile console target's rules), prompt detection
  (ADR-0082) and a `ConsoleFileSink`. Finished `ConsoleLine`s, prompt-seen and connection events go to the owner
  through the session's bounded event queue (`DropOldestMatching`: only data lines are dropped, never connection
  events). The sink is flushed by a BLE-thread timer (≤ 250 ms) and on link close.
- File I/O on the BLE thread is bounded per flush by `ConsoleFileSink::maxBufferedBytes`; one link's flush may delay
  the others by at most one such write.
- `ConsoleFileSink` opens by `SessionKey` (J-Link SN or BLE id). Default file names: J-Link unchanged
  (`<profile>-<target>-YYYYMMDD-HHMMSS-<sn>.log`); BLE `<profile>-ble-YYYYMMDD-HHMMSS-<sanitised id>.log`, where the
  id is sanitised as in the `BleLock` file name (`[A-Za-z0-9-]` kept, everything else → `-`).
- Retention parses the session kind from the file name and asks `ProbeLock::owner` or `BleLock::owner`; the
  live-file rules of plan 0002 (R45/R48) apply to both.

## Consequences

- No console file I/O on the GUI thread for any transport; owner-thread code is the same for J-Link and BLE
  (receives finished lines).
- The BLE thread does small, bounded file writes; handlers stay otherwise short (ADR-0080).
- `ConsoleFileSink` gains a key type; the J-Link call sites pass the SN unchanged.
