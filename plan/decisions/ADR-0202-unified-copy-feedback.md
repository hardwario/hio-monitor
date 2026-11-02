# ADR-0202 — One "Copied to clipboard." toast for every copy action

- **Status:** Accepted (plan [0028](../features/0028-gui-feedback-round-2.md), approved 2026-11-02)
- **Date:** 2026-11-02
- **Decided by:** Matej Strnad (customer feedback, GUI feedback round 2)

## Context

The old app confirmed every copy with "Copied to clipboard". v2 is inconsistent: console copy (menu, Ctrl+C) gives
no feedback (only a toast when the copy limit cut the text), Error panel / Warning banner Copy give none, About and
wizard copies say "Copied.", the report dialog says "Summary copied.".

## Options considered

1. Per-site texts (today).
2. **One text, "Copied to clipboard.", shown by one backend function for every copy action (chosen).**

## Decision

- Every copy action of the app shows the toast "Copied to clipboard." through `AppViewModel::showToast` (bottom
  centre, design system Toast). The clipboard write and the toast happen in one backend place
  (`AppViewModel::copyToClipboard` and the hooks that ViewModels receive from it), not in QML.
- No toast when there is nothing to copy (empty console selection, empty text).
- Covered: console copy (menu and Ctrl+C), Error panel and Warning banner Copy, Settings → About copies, wizard
  `CommandBlock` copies, report "Copy summary".
- Truncation, native text-field Ctrl+C and toasts above modal dialogs: plan 0028 decisions table.

## Consequences

- The design system's CommandBlock note ("toast 'Copied'") changes to the unified text; old texts leave the `.ts`
  files.
