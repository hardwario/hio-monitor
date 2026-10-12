# ADR-0046 — Console find bar and filter

- **Status:** Accepted; layout superseded by ADR-0056
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 GUI planning session)

## Context

The old app mixed search and filter behind an unlabeled icon toggle, paused the stream silently while searching,
treated input as a raw regex and placed navigation buttons far from the log.

## Options considered

1. **Browser-style find bar + separate always-visible filter (chosen).**
2. One combined field with a labelled Search/Filter switch.

## Decision

- **Find bar** (Ctrl+F) slides in above the focused console view (shell or log): text field, match counter
  "3 / 128", ↑ / ↓ buttons (Enter / Shift+Enter, F3 / Shift+F3; `n`/`N` in the view as before), toggles
  **Aa** (case) and **.\*** (regex), Esc closes. Matches are highlighted in all visible rows, current match
  more strongly. Search runs over the whole channel buffer incrementally (chunks of 5 000 lines per event-loop
  turn) so the UI never stalls.
- **Filter** row in the log toolbar: text field (Ctrl+L, same Aa / .\* toggles) + level chips
  **Debug / Info / Warning / Error** (multi-select). Non-matching lines are hidden; a counter
  "Showing 1 204 of 98 311 lines" and a Clear-filter button are visible while active.
- Plain text is the default; an invalid regex shows an inline error under the field and keeps the last valid result.
- The stream **never pauses silently**: the view has a **Follow** toggle (auto-scroll). Scrolling up or jumping
  to a match turns Follow off and shows a "▼ 312 new lines" pill; clicking it (or End) resumes following.
  An explicit **Pause** button freezes the *view* (data keeps being recorded) and is shown as a banner.

## Consequences

- Search and filter can be combined; both are view state in `ConsoleViewModel`, nothing changes the recorded data.
- Requires incremental search/filter workers on the GUI thread (time-sliced), unit-tested on 100 000 lines.
