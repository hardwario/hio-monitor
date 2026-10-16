# ADR-0113 — Console views: row selection only (no partial selection within a row) in M3

- **Status:** Accepted; supersedes ADR-0045 in its "partial selection in the focused row" part only
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (review round 1 of plan 0005)

## Context

ADR-0045 says that, besides row-range selection, "within a single row the user can also select part of the text
via a selectable `TextEdit` delegate on focus". Plan 0005's UX lists row selection only (click, Shift+click,
drag, Ctrl+A; Ctrl+C copies rows), and the M3 implementation built exactly that. The implementation notes of
0005 raised the gap as open point 3.

A selectable text overlay for the focused row adds a second selection model (characters inside a row next to the
row range), focus and keyboard rules for it (which selection does Ctrl+C copy, what Shift+arrow does), and more
delegate work in a view that has to stay fast at 100 000 lines per channel.

## Options considered

1. **Row selection only for M3; partial selection later if users ask for it (chosen).**
2. A selectable `TextEdit` overlay for the focused row now (ADR-0045 as written).

## Decision

- In M3 the console views select **whole rows only**: click, Shift+click, drag and Ctrl+A select rows; Ctrl+C,
  the context menu's Copy and Save view work on rows (with timestamps when they are shown). This is what plan 0005
  describes and what is implemented.
- ADR-0045's sentence about selecting part of a row's text with a selectable `TextEdit` delegate on focus is
  superseded. Everything else in ADR-0045 stays in force (virtualised `ListView` over `ConsoleLineModel`, plain-text
  rows, the copy cap, the 4 KiB display limit).
- Partial selection within a row may be added later if users ask for it; it then needs its own plan and ADR
  (selection model, keyboard rules, copy behaviour).

## Consequences

- One selection model (row ranges by sequence number) — simpler code, tests and keyboard behaviour.
- Users who need part of a line copy the row and trim it elsewhere; the plan's Risks already name a selectable
  overlay as the later remedy.
- The rows with search matches use a read-only plain-text `TextEdit` only to place the highlight rectangles; it is
  not selectable and takes no focus.
