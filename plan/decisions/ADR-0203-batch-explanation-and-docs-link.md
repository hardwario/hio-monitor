# ADR-0203 — The Batch action explains itself and links to the documentation

- **Status:** Accepted (plan [0028](../features/0028-gui-feedback-round-2.md), approved 2026-11-02)
- **Date:** 2026-11-02
- **Decided by:** Matej Strnad (customer feedback, GUI feedback round 2)

## Context

Console → "Batch…" (Ctrl+B) opens a file dialog with no explanation of what a batch file is or how it is sent.

## Options considered

1. Leave as is; document only.
2. **A longer explanatory tooltip plus a link to the user documentation (chosen).**
3. An explanatory dialog before the file dialog.

## Decision

- A plain-text tooltip (HToolTip) on the Batch action: each line of a UTF-8 text file is sent as a shell command,
  200 ms apart; empty lines and lines starting with `#` are skipped; the limits (10 000 commands, 1 MiB file,
  4 096 bytes per line) and the shortcut. The text and numbers come from the ViewModel (from the `BatchRunner`
  constants).
- A link to the user documentation; how it is presented and the final URL are decided with plan 0028 / plan 0029
  (customer docs on docs.hardwario.com). Until 0029 the link target comes from one central URL table.

## Consequences

- `HToolButton` gets an optional long description in its tooltip with a bounded width.
- ADR-0138's "docs/ on GitHub" link target is expected to be superseded by plan 0029; this ADR only depends on the
  URL table, not on where the page lives.
