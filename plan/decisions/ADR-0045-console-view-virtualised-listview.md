# ADR-0045 — Console views: virtualised ListView over a C++ line model

- **Status:** Accepted; the partial selection within a row is superseded by ADR-0113 (2026-10-16) (amended by ADR-0169, 2026-10-20)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 GUI planning session)

## Context

Shell and log hold up to 100 000 lines per channel (`ConsoleLog`, M1). The old app used a TextArea-like view
limited to 100 000 lines that became slow; trimming and colouring large text documents is costly.

## Options considered

1. **`ListView` over a C++ `QAbstractListModel`, only visible rows instantiated (chosen).**
2. `TextEdit` / `TextArea` with a growing document.

## Decision

- `ConsoleLineModel : QAbstractListModel` (app/gui) indexes the session's `ConsoleLog` for one channel
  (owner thread = GUI thread, as `ConsoleLog` requires). Roles: `seq, text, level, direction, time, matchRanges`.
- Appends are coalesced: at most **one model update per 50 ms**; ring-buffer eviction removes rows from the top
  (`beginRemoveRows`) in the same update.
- **Selection** is row-range based in the ViewModel (click, Shift+click, drag, Ctrl+A); within a single row the
  user can also select part of the text via a selectable `TextEdit` delegate on focus. Copy = selected rows
  joined with `\n` (optional timestamps). Copy is capped at **100 000 lines / 16 MiB**.
- Row delegate: monospace `Text` (`textFormat: Text.PlainText`; never rich text from device data),
  elided to **4 KiB** per line (the assembler limit), wrapping optional (setting).
- Filtering uses a second index vector (`FilteredIndex`, bounded by the channel's 100 000 lines).

## Consequences

- Memory and frame time stay bounded regardless of throughput.
- Free-form text selection across rows is not possible (rows are the unit); this is acceptable and matches the
  log-viewer model.
