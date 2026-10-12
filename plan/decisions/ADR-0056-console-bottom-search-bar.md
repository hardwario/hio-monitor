# ADR-0056 — Console search and filter in one bar at the bottom

- **Status:** Accepted (supersedes the layout part of ADR-0046; matching, Follow/Pause and "new lines" behaviour of ADR-0046 stay)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 GUI review of mockups)

## Context

ADR-0046 put a find bar and a filter row above the log. Users of the old app are used to the search field at the
bottom of the log. The old app's unlabeled mode icon and its silent pause were the problems, not the position.

## Options considered

1. Bottom find bar, filter stays on top.
2. **One bottom bar for both, with a labelled Search | Filter switch, like the old app (chosen).**
3. Find and filter rows stacked at the bottom.

## Decision

- Under the log is an always-visible **search bar**, from left to right:
  - a segmented switch **Search | Filter**;
  - the text field with **Aa** (case) and **.\*** (regex) toggles;
  - mode-specific controls:
    - Search: "3 / 128", ↑ ↓, × (clear);
    - Filter: "Showing X of Y lines", Clear filter;
  - the level chips **D I W E**, always visible.
- **Ctrl+F** focuses the bar in Search mode and **Ctrl+L** in Filter mode. Each mode keeps its own query, so both can
  be active together. An active filter stays visible as a chip "Filter: ctr_ ×" while the bar is in Search mode.
- Keys in the field:
  - Enter / Shift+Enter: next / previous match;
  - Esc: clears the search query, a second Esc returns focus to the log.
  In the focused log view, `n` / `Shift+N` and F3 / Shift+F3 also step through matches.
- The **shell** pane gets the same bar with Search only. It is hidden until Ctrl+F is pressed in the shell and
  appears directly above the command input.
- Search never pauses the stream. Jumping to a match turns Follow off (ADR-0046).

## Consequences

- The FindBar and FilterRow components become one `ConsoleSearchBar` component. The ViewModel API of
  `ConsoleLineModel` (find and filter properties) is unchanged.
