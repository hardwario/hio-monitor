# ADR-0169 — Console views scroll by whole lines

- **Status:** Accepted (amends ADR-0045)
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (GUI feedback round 1, plan 0021)

## Context

The console views are `ListView`s with the default Flickable behaviour: pixel scrolling with kinetic coasting. In a
log this feels imprecise; rows end up cut at the top.

## Decision

- Mouse wheel: one notch = the system's "lines per scroll" (`QStyleHints::wheelScrollLines`, default 3) whole rows,
  no kinetic coasting. Touchpad (pixel deltas) is accumulated and applied in whole rows.
- After any scroll (wheel, keys, scroll bar release) the top row is aligned to a row boundary.
- Keys in a focused view: Up/Down one row, PgUp/PgDn one page minus one row, Ctrl+Home / Ctrl+End to the start / end
  (Ctrl+End turns Follow on).
- Applies to the shell and the log view. Follow mode semantics (ADR-0045, 0005) are unchanged: scrolling up turns it
  off, reaching the end turns it on.

## Consequences

- Deterministic scrolling; wrapped rows of different heights scroll by row, not by pixel.
