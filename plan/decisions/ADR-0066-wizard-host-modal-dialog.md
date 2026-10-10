# ADR-0066 — Wizards are shown in a modal dialog

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M4 planning session)

## Context

The M3 shell (0004) has device tabs, a rail and a page area. Wizards are either global (first-run setup) or
belong to one device tab (connect, troubleshooting, flash). They must work at the minimum window size 960 × 600,
also in Czech, and be keyboard friendly.

## Options considered

1. **A modal dialog over the window (chosen).**
2. A page inside the device tab (replaces the page area; needs another host for the global setup wizard).
3. A side drawer next to the page (too narrow at 960 px).

## Decision

- One `WizardDialog` component (`Popup`, modal, centred, `min(720, window − 48) × min(560, window − 48)` px):
  - a header with the wizard title, the step list ("● Probe ○ Cable ○ Power ○ Check"; compact "Step 2 of 4"
    below 800 px width) and × (Cancel);
  - the step content (illustration, text, check list or embedded control such as the probe picker);
  - a footer with [Back] [Next | Finish | Check again] [Cancel].
- Per-tab wizards are bound to the tab that opened them and act on its session. The setup wizard is global.
- **Only one wizard is open at a time.** Opening another one while a wizard is open is refused: the launcher brings
  the open one to the front.
- Esc = Cancel (asks only when an operation started by the wizard is running). Enter = the default button.
- While a wizard runs an operation (e.g. flashing), the tab is busy (0004 `setBusy`), and the dialog's Cancel
  cancels the operation first.

## Consequences

- A simple focus and keyboard model, one implementation for all wizards.
- The user cannot look at the console while a wizard is open; wizards that need it end with a hand-off button
  ("Open console").
