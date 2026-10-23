# ADR-0155 — `ConsoleLog` reports per-channel totals and the oldest stored line

- **Status:** Accepted
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (M6 implementation session, sync of plan 0013 with the code)

## Context

The MCP `status` tool reports `terminal_total`/`log_total`, and `read_log`/`read_terminal` report `dropped_before`
when a cursor points to evicted lines. `ConsoleLog` (ADR-0063) has one global `seq` across both channels, a size per
channel, and no counters for appended or evicted lines.

## Options considered

1. **Extend the `ConsoleLog` API** with cheap counters.
2. Drop the fields from the MCP tools.

## Decision

Option 1. `ConsoleLog` gains `[[nodiscard]] std::uint64_t total(Channel) const` (lines ever appended to the channel,
markers included, not reset by eviction; `clear()` does not reset it) and `[[nodiscard]] std::optional<std::uint64_t>
firstSeq(Channel) const` (seq of the oldest stored line, `nullopt` when empty). No behaviour change for existing
users.

## Consequences

- `dropped_before` = number of lines of that channel with seq > cursor that were evicted, computed from
  `firstSeq`; documented as "lines lost before the first returned line".
