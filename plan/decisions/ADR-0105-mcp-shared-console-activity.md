# ADR-0105 — Shared console without locking; AI activity shown as console markers and an activity list

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (M6 planning session)

## Context

In the GUI the AI works on the same live session the user sees (ADR-0008). Both can type into the shell, and the
user must be able to see what the AI did.

## Options considered

- Shared shell: **no lock, echo-based attribution (chosen)** / lock the input while an AI command runs.
- Visibility: **console markers + MCP panel + rail indicator (chosen)** / panel only / markers only.

## Decision

- **No input lock.** `send_command` takes output after its own echo. When another command's echo (from the user
  or another AI call) appears before `send_command` ends, the result carries `interleaved: true` and the other
  echo stays in the output so the AI can see it. While an AI command runs, the console input shows the hint "AI
  is running a command".
- Operations stay mutually exclusive through `internal.busy`. An AI operation sets the tab busy (ADR-0059's
  `setBusy`) with the text "AI: Flashing…" and a Cancel button. Cancel ends the tool call with `cancelled`.
- **Markers:** every AI tool call on a device, except plain reads (`read_log`, `read_terminal`, `status`,
  `list_devices`, `read_memory`, `read_registers`, `memory_zones`, `target_status`), writes a marker into that
  tab's `ConsoleLog`, for example "— AI (Claude Code): flash app.hex —" and "— AI: flash done in 12.4 s —".
  `send_command` marks the command, and its echo shows the text.
- **Activity list:** the last **1000** calls (time, client, tool, device, short arguments, result code, duration),
  for every call including reads. In the GUI it is on the MCP settings section. In the CLI it goes to the log file
  (`hio.mcp` category) instead.
- **Rail indicator** (GUI): an "MCP" icon above Settings on the rail (the window has no header bar, ADR-0043)
  with the states off (hidden) / listening / N clients / error. It opens the MCP section.

## Consequences

- The user can follow and stop the AI without new modes. The AI learns about interference from `interleaved`.
