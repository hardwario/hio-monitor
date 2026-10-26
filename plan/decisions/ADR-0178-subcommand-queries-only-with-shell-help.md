# ADR-0178 — Subcommand queries (`<command> -h`) only when the shell's `help` shows `-h` support

- **Status:** Proposed (amends ADR-0170: how subcommands are learned)
- **Date:** 2026-10-26
- **Decided by:** Matej Strnad (plan 0022)

## Context

ADR-0170 learns the device's subcommands by sending `<command> -h` silently the first time the user types past a
known command. The app sends that to commands it knows nothing about. Whether `-h` is harmless depends on the
firmware:

- In the Zephyr shell core (`subsys/shell/shell.c`, `execute()`), with `CONFIG_SHELL_HELP` enabled, a `-h` /
  `--help` word after a command is handled by the core: it prints that command's help (or returns `-ENOEXEC` when the
  command has no help text) and **never calls the command's handler**.
- With `CONFIG_SHELL_HELP` disabled, or on a firmware that is not a Zephyr shell, `-h` is just an argument. A command
  that ignores its arguments runs (for example a configuration reset or a reboot).

The Zephyr `help` command is always registered. It prints the line
`You can try to call commands with <-h> or <--help> parameter for more information.` only under
`#if defined(CONFIG_SHELL_HELP)`. `Available commands:` is printed in both cases (with help texts when
`CONFIG_SHELL_HELP` is on, bare names when it is off). Source checked: Zephyr `main`, 2026-10-26; the Zephyr
version of the CHESTER SDK is not checked yet.

## Options considered

1. Keep `<command> -h` for every known command (ADR-0170 as is).
2. Top level from `help` only; subcommands from history only, never `-h`.
3. Pass Tab to the device's own completion (rejected in ADR-0170: the RTT console is line-based, ADR-0033).
4. Send `-h` only when the `help` answer shows that the shell handles `-h` itself (chosen).

## Decision

- The answer to `help` is checked for the `CONFIG_SHELL_HELP` marker: a line containing
  `<-h> or <--help>` (ANSI sequences removed, the same cleaning as `parseHelp`).
- Marker present → subcommands are learned with `<command> -h` exactly as in ADR-0170.
- Marker absent (or no `help` answer) → **no `-h` query is ever sent** in this attach. Completion uses the
  top-level commands from `help` and the history; subcommands come from history only.
- The result is stored with the cached tree per firmware (ADR-0170 cache key), so a cached firmware does not need
  `help` again to know it.
- Same for the GUI and the CLI console (shared `CommandTreeLearner` in `libs/device`).

## Consequences

- No command the user did not type is ever run with an argument the firmware may not understand.
- Firmware built without `CONFIG_SHELL_HELP` gets no subcommand suggestions from the device.
- The check depends on Zephyr's wording of one line. If the wording changes, the app falls back to the safe side
  (no `-h`). The wording on CHESTER firmware must be confirmed in HIL (plan 0022).
- The cache format changes (schema 2).
