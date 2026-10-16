# ADR-0104 — `send_command` ends at the shell prompt, else after silence, always within a timeout

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (M6 planning session)

## Context

rttt waits up to `timeout` and, once output starts, stops after 300 ms of silence. That cuts off commands which
pause mid-output. `hio chester app command` collects for a fixed window, which makes every call slow. BLE already
recognises the profile's prompt (ADR-0082). The J-Link RTT path does not.

## Decision

- `send_command(device, command, timeout=5, target?)` with `timeout` between 0.1 and 120 s:
  1. Record the cursors, send the line, and wait for its echo (`dir = in`, M1). Output before the echo is not
     part of the result.
  2. End when the **shell prompt** is seen after the echo (`end: "prompt"`). The silence rule does not apply
     while a prompt is expected and the profile has one.
  3. Without a known prompt: end after **300 ms without new shell output** once output has started
     (`end: "silence"`).
  4. Always end at the absolute `timeout` from the send (`end: "timeout"`; this is not an error).
- **Prompt for RTT:** `TargetProfile` gains `shellPrompt`. `DeviceSession` recognises it like ADR-0082 does
  (pending partial line equals the prompt after ANSI stripping) and emits `promptSeen`; `ConsoleSession`
  docs are updated ("BLE and J-Link"). The CHESTER value must be confirmed on hardware (open question 25).
  Until it is, `shellPrompt` is empty and the silence rule applies.
- **Result:** `{command, output:[lines], end, interleaved, log_cursor, log_cursor_end, terminal_cursor_end,
  truncated}`. Output is capped at 2000 lines / 256 KiB.

## Consequences

- Fast commands return as soon as the prompt arrives, and slow ones are not cut off.
- The RTT prompt detection also benefits `hio chester app command` later (not changed in M6).
