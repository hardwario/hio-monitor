# ADR-0201 — "Save view…" proposes a file name and remembers the folder

- **Status:** Accepted (plan [0028](../features/0028-gui-feedback-round-2.md), approved 2026-11-02)
- **Date:** 2026-11-02
- **Decided by:** Matej Strnad (customer feedback, GUI feedback round 2)

## Context

Console → "Save view…" opens a save dialog with an empty name and no folder. On Linux, pressing Save with a folder
selected only enters the folder, so the action looks broken. The diagnostic report dialog (`ReportDialog` /
`ReportViewModel`) already prefills folder and name and works.

## Options considered

1. Leave the dialog empty.
2. **Prefill the name from the console file name base and start in the last used folder (chosen).**

## Decision

- The dialog's `selectedFile` is prefilled with a name built by the same backend function as the console file name
  base: with a known firmware e.g. `chester-clime-v4.0.0-<stamp>-shell.txt` / `…-log.txt` (per channel), otherwise
  the fallback base without firmware (exact slug per plan 0028).
- The folder is the last folder a view was saved to (persisted setting `console/saveViewFolder`); the first time, or
  when that folder no longer exists, the user's Documents folder.
- The ViewModel provides folder and name (ADR-0003: no logic in QML); a non-local URL is refused like
  `ReportViewModel::save` (exact feedback per plan 0028).

## Consequences

- One new setting key; the shared base-name function lives in `libs/device` next to the console file naming.
