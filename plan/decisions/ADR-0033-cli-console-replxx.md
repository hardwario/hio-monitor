# ADR-0033 — CLI interactive console: line mode with replxx

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M1 planning session)

## Context

`hio chester app console` replaces py-hardwario's console, which uses rttt's full-screen
prompt_toolkit TUI (split shell/log panes). 0006-cli left open "simple line mode vs. split TUI".
Device output arrives on a worker thread while the user types.

## Options considered

1. Scrolling line mode with **replxx** (BSD-3, C++, Linux/macOS/Windows): line editing, persistent history,
   Ctrl+R search, colors, thread-safe printing above the prompt while editing (chosen).
2. Full-screen TUI with FTXUI (MIT): rttt-like panes; much more scope and a larger dependency.
3. Plain line mode without a library: no editing, no history.

## Decision

- The interactive console is a **scrolling line mode built on replxx**, fetched via CPM pinned to tag +
  SHA-256 (ADR-0010). Shell and log lines are interleaved; log lines are colored by level; the prompt
  stays at the bottom.
- When stdin is not a TTY, or with `--json`, replxx is not used: plain line input, NDJSON output (ADR-0026).
- A full-screen TUI is out of scope for v2.0 (the GUI covers the split-view use case).

## Consequences

- New dependency: replxx (BSD-3). It must build cleanly with clang / clang-cl under our warning flags
  (third-party targets do not get our warning flags, per the M0 plan) and pass sanitizers.
- Only the CLI links replxx; the backend stays independent of it.
