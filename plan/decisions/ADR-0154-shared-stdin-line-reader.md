# ADR-0154 — `hio mcp` reuses a parametrised `StdinLineReader`

- **Status:** Accepted (amended by ADR-0163: byte budget from the settings file)
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (M6 implementation session, sync of plan 0013 with the code)

## Context

The CLI console already has `StdinLineReader` (`app/cli/src/terminal.h`): an owned `std::jthread`, 50 ms poll,
`CancelSynchronousIo` on Windows, a 256-line queue and lines cut at 4096 bytes; the consumer polls. Plan 0013
specified a second reader for MCP stdio (4 MiB lines, 64-line queue, queued-signal wake-up).

## Options considered

1. **Extend the existing reader** with parameters.
2. A new class for MCP.

## Decision

Option 1. `StdinLineReader` gets an options struct: maximum line length, queue capacity, overlong-line policy
(`Truncate` = today's behaviour, `DropAndReport` = drop the rest of the line and deliver an "overlong" marker), and an
optional notify callback that the reader thread calls after each push (MCP uses it to post a queued wake-up to the
owner thread). The console keeps its current values (4096 bytes, 256 lines, `Truncate`, no callback); `hio mcp` uses
4 MiB, 64 lines, `DropAndReport` and the callback. The queue blocks when full (backpressure).

## Consequences

- One stdin implementation to maintain and to test under TSan.
