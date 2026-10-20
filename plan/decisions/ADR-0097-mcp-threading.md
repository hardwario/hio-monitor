# ADR-0097 — MCP server on the owner thread with asynchronous tool handlers

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (M6 planning session)

## Context

`DeviceSession`, `ConsoleLog` and `SessionRegistry` may be used only on their owner thread (ADR-0031, ADR-0063):
the GUI main thread, or the CLI main thread. Several tools wait for seconds or minutes (`send_command`,
`wait_for_state`, `wait_for_connection`, `flash`). Several clients or requests can be in flight at once.

## Options considered

1. **Everything on the owner thread, with async handlers (chosen).**
2. A dedicated MCP thread that marshals every tool call to the owner thread.

## Decision

- `McpServer`, both transports, the dialects and all tool handlers live on the **owner thread**.
- A tool handler returns a `ToolCall` object, a small state machine driven by session signals and `QTimer`s.
  It completes once, with `Result<ToolResult>`, or ends on cancellation. **No nested event loops** (no
  `QEventLoop::exec`, no `waitFor*` helpers) in `libs/mcp`.
- Bounds: at most **32 in-flight tool calls** per server (`mcp.too_many_requests` beyond that). JSON parsing of a
  request is bounded by the body limit (ADR-0102), so it cannot stall the GUI for long.
- The CLI `hio mcp` runs `QCoreApplication::exec()`. This is the first CLI command that does; the others keep
  their nested loops. stdin is read by one owned `std::jthread`, which posts complete lines through a bounded
  queue (64 lines) and a queued signal. The reason is that Windows cannot watch stdin with `QSocketNotifier`.
  stdout is written only on the owner thread.
- Shutdown: stdin EOF, SIGINT/SIGTERM or `McpServer::stop()` cancels every call, closes sessions the server opened,
  and joins the reader thread (it is stoppable by closing stdin on POSIX; on Windows `CancelIoEx`).

## Consequences

- No new cross-thread state, so TSan stays simple. Each handler is unit-testable with a fake clock.
- A slow handler would freeze the GUI, so handlers must never block. This is enforced in review and by a test
  that measures owner-thread stalls under load.
