# ADR-0161 — `erase scope=all` and `recover` need the `erase` and `uicr` categories (amends ADR-0101)

- **Status:** Accepted
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (M6, after the independent review of 0013 phases A–C)

## Context

`erase scope=all` and `recover` wipe the whole chip, including the PIB (serial number, claim token). The plan table
gave them only the `erase` category, while ADR-0101 says the `uicr` category protects the PIB.

## Decision

Over MCP, `erase` with `scope=all` and `recover` require both `erase` and `uicr` (CLI: `--allow erase,uicr` or
`all`; GUI 0014: one dialog listing both). `erase scope=code` keeps `erase` only. The CLI commands themselves are
unchanged (they already ask, ADR-0038).

## Consequences

- An AI client allowed only to erase code can never remove the PIB.
