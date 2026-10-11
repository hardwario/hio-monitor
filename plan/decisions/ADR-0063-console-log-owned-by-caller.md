# ADR-0063 — The console log is provided by the session's owner

- **Status:** Accepted (amends feature 0002)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 planning review)

## Context

In 0002 each `DeviceSession` owns its `ConsoleLog` (100 000 lines per channel). A GUI tab creates a new session on
every Connect after a Disconnect, and also when it changes probe or target, so the console text would disappear. The
old app kept the text, and users rely on scrolling back after a reconnect.

## Options considered

1. **The session appends into a `ConsoleLog` that its owner provides (chosen).**
2. The GUI model keeps its own bounded copy of the lines (doubles the console memory).
3. Clear the view on a new session (the lines remain only in the console log file).

## Decision

- `SessionOptions` gains `std::shared_ptr<ConsoleLog> consoleLog`. If it is `nullptr`, the session creates its own
  log (the CLI does this). `DeviceSession::log()` returns the log in use.
- `ConsoleLog` stays single-owner-thread (the façade's thread). The `shared_ptr` only extends its lifetime beyond
  one session; it is never accessed from the worker.
- A GUI tab creates one `ConsoleLog` and passes it to each of its sessions. The bounds are unchanged
  (100 000 lines per channel, oldest evicted).
- `ConsoleLine::Direction` gains `Marker`. `ConsoleLog::appendMarker(Channel, std::string text)` adds a marker line
  (e.g. "— connected —", "— disconnected: target has no power —", "— paused: Flashing… —"). The GUI adds markers on
  connection changes and busy changes. The CLI does not use markers (it prints connection events itself), and
  `--json` never emits `dir = marker`.
- Clear (GUI) clears the tab's log for the chosen channels. Cursors stay monotonic (`linesSince` continues to work).

## Consequences

- The console history of a tab survives Disconnect/Connect and changes of probe or target, at no extra memory.
- Feature 0002 changes `SessionOptions`, `ConsoleLine` and `ConsoleLog` (small API additions plus tests).
- Feature 0005 binds `ConsoleLineModel` to the tab's log, not to the session. A session change no longer resets
  the model.
