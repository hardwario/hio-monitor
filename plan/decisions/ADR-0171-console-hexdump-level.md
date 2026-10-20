# ADR-0171 — Hexdump lines get their own level, hidden by default

- **Status:** Accepted (amends ADR-0046)
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (GUI feedback round 1, plan 0021)

## Context

Zephyr `LOG_HEXDUMP_*` prints a header line with a level tag followed by continuation lines such as
`        44 74 4f 34 78 54 73 31  4b 36 69 41 73 41 53 4a |DtO4xTs1 K6iAsASJ`. The continuation lines have no level
tag, so the level chips (D I W E) never hide them: with Info switched off, pages of hex bytes stay in the log.

## Decision

- A new log level **Hexdump** (`LogLevel::Hexdump`, id `hex`, chip **H**) is assigned in `libs/device` to lines that
  match the Zephyr hexdump continuation format: leading whitespace, 1–16 two-digit hex groups (optional double space
  after the 8th), then `|` and up to 16 printable characters. The header line keeps its own level.
- The **H chip is off by default** in the log view of every new console page; the other chips stay on. "Showing X of
  Y lines" makes the hidden lines visible in the count. The chip state is per view, not persisted (as the other chips).
- Search, copy, save and log files keep the lines; console files / JSON write the level as `hex`.
- Other lines without a level tag (shell output, `printk`) stay unaffected by the chips.

## Consequences

- Hexdumps no longer flood the log; one click shows them.
- The detection is a pattern; a non-hexdump line that matches it exactly would be hidden too (unlikely; unit-tested
  on real CHESTER output).
