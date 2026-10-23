# ADR-0173 — Console scroll bars sit beside the rows, never over them

- **Status:** Accepted (amends ADR-0045; 0021 amendment N10, approved 2026-10-23 including the Follow suspension and
  drag auto-scroll)
- **Date:** 2026-10-23
- **Decided by:** Matej Strnad (GUI feedback round 1, plan 0021)

## Context

Users cannot select or copy the last log line when the view is at the very end: "a bar gets in the way". The console
`ListView` uses attached scroll bars (`ScrollBar.horizontal` / `ScrollBar.vertical` declared inside the list). Qt Quick
Controls places attached scroll bars **over** the Flickable's content. With the Material style the horizontal bar is
16 px high, the same as a row at the default font, and is shown whenever a line is wider than the view (wrap is off by
default; hexdump and long log lines are common). At the end of the log it covers the last row completely.

Reproduced on 2026-10-23 with the project's Qt Quick test runner (offscreen, Material): at the end, the bar spans
y 184–200 of a 200 px list and the last row spans 184–200; a press anywhere on the last row reaches the scroll bar,
the row gets **0** presses. A `bottomMargin` alone did not help (`positionViewAtEnd()` ignores it). The same list
with the horizontal `ScrollBar` declared as a sibling below the list (assigned through `ScrollBar.horizontal: hbar`)
shows the last row at 168–184, the row gets the press, and the bar still scrolls the content. The vertical bar
covers the right 16 px of every row in the same way.

## Options considered

1. Keep the attached bars and add `bottomMargin` / `rightMargin` (fragile: follow and `positionViewAtEnd` ignore the
   margin; verified not to help).
2. Declare both bars outside the list, in the layout next to it (chosen).
3. Turn wrap on by default so the horizontal bar never appears (changes the log layout; the vertical bar issue stays).

## Decision

- In `ConsoleView`, the horizontal scroll bar is a sibling **below** the list and the vertical one a sibling **to the
  right** of it (layout), connected with `ScrollBar.horizontal: hbar` / `ScrollBar.vertical: vbar`. They take layout
  space only while visible (`AsNeeded`); the list's height/width shrink, and the follow logic re-positions on
  `heightChanged` as today.
- While a row is pressed (click or drag selection) the view does not follow: Follow is suspended from press to
  release, so rows do not move under the pointer at the end of a busy log; after release, Follow resumes if the view
  is still at the end.
- Dragging a selection past the top or bottom edge scrolls by whole rows (ADR-0169) and extends the selection.

## Consequences

- The last row is always fully visible and selectable; no content is hidden behind a bar.
- The bars no longer float over the text; the view loses 16 px only while a bar is needed.
- A Qt Quick test covers it: at the end with a long line, a press on the last row selects it and Ctrl+C copies it.
