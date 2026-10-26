# 0022 — Send `<command> -h` only to shells that handle it (console completion safety)

- **Status:** Draft
- **Milestone:** GUI feedback follow-up (after 0021)
- **Author:** Matej Strnad
- **Related ADRs:** ADR-0178 (new), ADR-0170 (amended), ADR-0175, ADR-0176, ADR-0166, ADR-0033
- **Depends on:** 0021 phase 2 (`v2/gui-feedback-1`, 651daac: `ShellQuery`, `CommandTreeLearner`, `CommandTreeCache`)

## Context

Plan 0021 (ADR-0170) learns the device command tree for console completion with silent shell queries: `help` for
the top level, then `<command> -h` the first time the user types past a known command. The app sends `-h` to
commands it knows nothing about. On a Zephyr shell with `CONFIG_SHELL_HELP` the shell core answers `-h` itself and
never runs the command; without it (or on a non-Zephyr firmware) `-h` is an ordinary argument and a command that
ignores arguments would run. The app must not run device commands the user did not type.

Zephyr's `help` prints a line with `<-h> or <--help>` only when `CONFIG_SHELL_HELP` is enabled (ADR-0178), so the
app can tell from the `help` answer it already reads.

## Goals / Non-goals

Goals:
- `<command> -h` is sent only when the `help` answer has the `CONFIG_SHELL_HELP` marker.
- The result is cached per firmware with the command tree.
- Without the marker, completion still works with top-level commands and history.
- Same behaviour in the GUI console and the CLI console.

Non-goals:
- Passing Tab to the device (ADR-0170 option 3 stays rejected).
- Detecting other shells than Zephyr.
- Changing when `help` is sent (`console/readCommandsOnConnect` stays as is).

## User experience

No new UI. With the marker: unchanged from 0021. Without the marker: the suggestion list shows top-level device
commands and history entries; after a command word only history entries match. No message is shown; the app log
(device category) has one info line: `Shell has no -h support (CONFIG_SHELL_HELP marker missing): subcommands are not
queried`.

## CLI

None (the CLI console uses the same learner; no new commands or options).

## Design

### `libs/device`

- `command_tree.h`
  - `[[nodiscard]] bool helpSupportsDashH(std::span<const std::string> lines);` — pure: true when a line (ANSI CSI
    removed, `\r` trimmed) contains `<-h> or <--help>`.
  - `CommandTree` gains `bool dashHSupported() const noexcept` and `void setDashHSupported(bool)`. Default false.
  - JSON: `{"schema":2,"dashH":bool,"root":[...]}`. `fromJson` rejects schema 1 (`usage.invalid_argument`), so an
    old cached tree is treated as missing and `help` is sent again once (decision D2).
- `parseHelp()` additionally accepts bare-name lines (`  <name>` with nothing after it) in the `Available commands:`
  list, the format Zephyr prints without `CONFIG_SHELL_HELP` (decision D3). Help text is empty for them.
- `CommandTreeLearner`
  - `learned({}, result)` (the `help` answer) sets `dashHSupported` from `helpSupportsDashH(result->lines)` and logs
    the info line above when false.
  - `requestChildren(path)` returns at once when the tree is missing the flag (`!m_tree->dashHSupported()`), before
    any query and before `m_attempted` is touched.
  - A cached tree carries the flag, so no `help` is needed for a known firmware.
- `Completer::pathToLearn()` is unchanged (it says what is unknown; the learner decides whether to ask).

### GUI / CLI

No change; both call `CommandTreeLearner::requestChildren()` (`console_view_model.cpp`, `console_ui.cpp`).

### Threading

Unchanged: owner thread of the session (ADR-0031); the flag lives in the immutable tree snapshot.

### Errors

None new. A missing marker is not an error.

### Limits

Unchanged (ADR-0170 bounds). The flag adds one boolean per cached firmware.

## Decisions requiring user confirmation

| # | Question | Options | Decision (by user) | ADR |
|---|---|---|---|---|
| D1 | How to avoid running commands with `-h` | keep `-h` / never `-h` / `-h` only with the `CONFIG_SHELL_HELP` marker in `help` | `-h` only with the marker (2026-10-26) | ADR-0178 |
| D2 | Old cache entries (schema 1, no flag) | discard and relearn with `help` / keep and assume no `-h` | _pending_ — proposed: discard (one extra `help` per known firmware) | ADR-0178 |
| D3 | Top-level names without `CONFIG_SHELL_HELP` (bare names, no help text) | parse them / ignore them | _pending_ — proposed: parse them | ADR-0178 |
| D4 | Marker match | substring `<-h> or <--help>` / the full Zephyr sentence | _pending_ — proposed: substring (survives small rewording around it) | ADR-0178 |

## Implementation steps

- [ ] `helpSupportsDashH()` + unit tests (marker present, absent, with ANSI codes and `\r`, split across output).
- [ ] `CommandTree` flag, JSON schema 2, schema 1 rejected; tests for round trip and old schema.
- [ ] `parseHelp()` bare-name lines (D3) + tests with real Zephyr output with and without `CONFIG_SHELL_HELP`.
- [ ] `CommandTreeLearner`: set the flag from `help`, gate `requestChildren()`, log line; tests with the fake
      `ShellQueryQueue`: no `-h` query without the marker, `-h` with it, cached flag used without `help`.
- [ ] Fake J-Link Zephyr shell scenario: a variant without the marker; GUI (`console_view_model_test`,
      `tst_console.qml`) and CLI (`console_learning_test`) tests prove no `-h` is sent.
- [ ] ADR-0170 status line: "amended by ADR-0178"; 0021 text (Design / Risks) points to ADR-0178.
- [ ] HIL (with the 0021 HIL run): the CHESTER `help` answer contains the marker; `<command> -h` on a command with a
      handler (e.g. one that changes state) prints help and does not run it.

## Tests and verification

- `ctest --preset linux-debug`, `-asan`, `-tsan` green; `linux-tidy` zero findings; clang-format clean.
- New unit tests above; existing 0021 completion and learning tests stay green (scenarios with the marker).
- HIL steps above, recorded in the plan when run. Until then the marker wording on CHESTER is unconfirmed: a
  mismatch only turns subcommand learning off (safe side).

## Risks and open points

- The marker check relies on one sentence of Zephyr's `help`. The Zephyr version in the CHESTER SDK may word it
  differently; HIL confirms it. If it differs, the substring (D4) is adjusted.
- A firmware could print the marker without the Zephyr core behaviour (a custom `help`). Unlikely; accepted.
- Silent `help` still appears in the device's own shell history (ADR-0170 consequence, unchanged).
