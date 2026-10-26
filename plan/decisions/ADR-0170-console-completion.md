# ADR-0170 — Console autocompletion: history suggestions and device commands

- **Status:** Accepted (amends ADR-0033 for the CLI; refined 2026-10-20: `help` on connect with a setting, automatic list; subcommand queries amended by ADR-0178 (Proposed))
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (GUI feedback round 1, plan 0021)

## Context

The shell input has history (Up/Down, Ctrl+R) but no suggestions or completion. CHESTER runs a Zephyr shell whose
`help` lists the commands; `<command> -h` / `<command> help` lists subcommands.

## Options considered

1. History only.
2. History suggestions + device commands learned from the shell (chosen).
3. Send Tab to the device shell (native Zephyr completion over RTT; the RTT console is line-based, ADR-0033).

## Decision

- **Inline suggestion (ghost text):** while typing, the most recent history entry (per profile/target, ADR-0058) that
  starts with the input is shown greyed after the cursor; → or End accepts it, Esc hides it.
- **Automatic suggestion list (no Tab needed):** while the input is non-empty, a list **above the input** shows the
  matching device commands for the current word (and subcommands after a known command), followed by matching history
  entries (clock icon); at most 8 rows visible, 50 in total, updated as the user types. Esc closes it until the next
  keystroke. It can be switched off (`console/suggestions`, default on).
- **Tab completion:** Tab completes the current word (the highlighted list row, or the common prefix of the
  candidates).
- **Device command tree:** learned by **silent shell queries** — a command sent by the app whose output is captured
  and not shown in the shell view (the one-shot capture of `hio chester app command`, end rule ADR-0104). The top level
  comes from `help` **immediately after the console attaches** (setting `console/readCommandsOnConnect`, default on;
  when off, `help` is sent the first time a suggestion is needed); subcommands of a command are queried lazily the
  first time the user types past it (`<command> -h`).
  Queries run only while the shell is idle and the tab is not busy; a user command waits until a query ends (≤ 2 s).
- The tree is cached per (profile, target, `fw-name`, `fw-version` from `info show`, ADR-0166) on disk, bounded
  (≤ 2 000 nodes, ≤ 64 KiB per firmware, ≤ 20 firmwares, LRU), so a known firmware needs no query.
- The CLI console gets the same: replxx hints from history and completion from the same tree and cache (libs/device),
  so the logic lives in the backend.

## Consequences

- Faster typing, discoverable commands, no device-specific lists in the app.
- The app sends commands the user did not type (`help`, `info show`, `<cmd> -h`); they are read-only and logged in
  the app log, but appear in the device's own shell history.
