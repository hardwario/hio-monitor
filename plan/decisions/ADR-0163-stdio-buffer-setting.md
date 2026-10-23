# ADR-0163 — Byte budget of the MCP stdin queue, configurable in the settings file (amends ADR-0154)

- **Status:** Accepted
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (M6, after the independent review of 0013 phases A–C)

## Context

ADR-0154 gives `hio mcp` a stdin queue of 64 lines of up to 4 MiB each: up to 256 MiB in the worst case. The user
asked for a byte budget that can be configured.

## Decision

The queue blocks when either 64 lines or the byte budget is reached. The budget is read at start from the settings
file (ADR-0025) key `mcp/stdioBufferMiB`: default 16, allowed 4–256; an invalid or out-of-range value is clamped
and a warning is logged to stderr/`hio.mcp`. A single line may still be up to 4 MiB (always ≤ the budget).

## Consequences

- Bounded memory by default; users with very large requests can raise it without a new build.
