# ADR-0176 — The J-Link session worker reads the target's buffers before it writes a line

- **Status:** Accepted (amends the M1 session worker behaviour, plan 0002)
- **Date:** 2026-10-23
- **Decided by:** Matej Strnad (GUI feedback round 1, plan 0021 phase 2)

## Context

ADR-0104 and ADR-0175 treat shell output that arrives before a command's echo as output of an earlier command. On
J-Link the echo is produced by the host when the worker writes the line, not by the device. The worker read RTT only
in its read cycle, so output already sitting in the target's up buffer when a line was written was read after the
echo and counted as the answer. A silent `ShellQuery` (plan 0021) then swallowed it; 8 existing GUI tests broke.

## Options considered

1. Before writing a sent line, the worker runs one read cycle (chosen).
2. Leave the worker unchanged and work around it inside `ShellQuery` only (the MCP `send_command` keeps the same flaw).

## Decision

- `SessionWorker::sendLine` runs one `readCycle()` before it writes the line. Output already buffered on the target
  is delivered before the echo.
- If that read fails, the link failure is reported as `Disconnected` and the line is not written (as a failed write
  was before).
- BLE is unchanged (its echo already follows the device's output order).

## Consequences

- One extra RTT read per sent line (the power check stays rate-limited).
- Both `ShellQuery` and MCP `send_command` get a correct "before the echo" boundary for buffered output. Output the
  device has not printed yet is not covered by this; that is handled by starting queries only on an idle shell
  (plan 0021 review P1).
