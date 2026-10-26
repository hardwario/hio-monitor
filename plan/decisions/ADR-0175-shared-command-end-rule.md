# ADR-0175 — One shared command-capture helper in `libs/device` for the ADR-0104 end rule

- **Status:** Accepted
- **Date:** 2026-10-23
- **Decided by:** Matej Strnad (GUI feedback round 1, plan 0021 phase 2)

## Context

Plan 0021 needs `ShellQuery`: a silent `info show` / `help` / `<cmd> -h` on a session. Its end rule is
ADR-0104's: wait for the echo, end at the shell prompt, otherwise after 300 ms of silence once output has started,
and always end at an absolute timeout. M6 implemented that rule as the private `SendCommandCall` in
`libs/mcp_tools/src/tools/console_tools.cpp`. `libs/device` cannot depend on `hio_mcp_tools`. The CLI has a third
pattern (`runCommand` / `CommandTiming` in `app/cli/src/command_collector.h`: fixed collect window, prompt end only
for BLE).

## Options considered

1. A shared helper in `libs/device` that implements the end rule once; `ShellQuery` and MCP `send_command` use it
   (chosen).
2. `ShellQuery` gets its own copy of the rule; M6 code untouched (two copies to keep in sync).

## Decision

- `libs/device` gains `CommandCapture` (`hio/device/command_capture.h`): created on the session owner's thread for one
  `ConsoleSession`, it sends one line and collects the shell output after the echo. It ends at the prompt
  (`TargetProfile::shellPrompt`, `promptSeen`), else after the silence interval (300 ms) once output started, and
  always at the absolute timeout. The result has the lines, the end reason (`prompt` / `silence` / `timeout`) and
  `truncated`. Output is bounded by the caller's limit (ADR-0104 2000 lines / 256 KiB; ShellQuery 2 000 lines, 2 s).
- MCP `send_command` (`SendCommandCall`) uses `CommandCapture` for the rule. Its result format, markers, AI hint
  and cursors stay in `libs/mcp_tools`. Behaviour must stay identical; the existing M6 tests prove it.
- `ShellQuery` (plan 0021) is built on `CommandCapture` and adds the silent part: captured lines are not appended to
  the console log, a user `sendLine` waits until a running query ends, and there is no query while busy.
- CLI: `hio chester app info` uses `ShellQuery`. `hio chester app command` keeps its fixed collect
  window (py-hardwario compatible -t/--timeout); it does not switch to the end rule.

## Consequences

- One implementation of the end rule; when the CHESTER prompt is confirmed (open question 25) all users benefit.
- 0021 phase 2 touches M6 code (`console_tools.cpp`); the M6 MCP tests must stay green.
- `CommandCapture` gets its own unit tests (prompt end, silence end, timeout, echo filtering, bound).
