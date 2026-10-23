# ADR-0095 — MCP protocol revisions: 2025-11-25 and 2026-07-28 (dual-era server)

- **Status:** Accepted (amended by ADR-0159: legacy `initialize` with an unknown version is answered with 2025-11-25)
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (M6 planning session)

## Context

The current MCP revision is **2026-07-28**. It is stateless: there is no `initialize` and no `Mcp-Session-Id`.
Instead, every request carries `_meta` (`io.modelcontextprotocol/protocolVersion`, `clientCapabilities`,
`clientInfo`), the server must implement `server/discover`, and the HTTP transport checks `MCP-Protocol-Version`,
`Mcp-Method` and `Mcp-Name`. There is no GET stream, and cancellation means closing the stream. Clients are
still migrating: Claude is "rolling out" 2026-07-28, while Cursor (and possibly VS Code) supports only revisions up
to 2025-11-25. A legacy client cannot talk to a modern-only server.

## Options considered

1. **Both revisions over one core (chosen).**
2. 2025-11-25 only: the widest support today, but a later migration.
3. 2026-07-28 only: the simplest, but it locks out current clients.

## Decision

- `libs/mcp` supports **2026-07-28** ("modern") and **2025-11-25** ("legacy"). Older legacy revisions
  (2025-06-18, 2025-03-26) are accepted in `initialize` and answered with 2025-11-25 semantics only where the
  wire is identical. Anything else gets the spec's version error, with the supported list.
- **Era detection per request:** a request with `_meta["io.modelcontextprotocol/protocolVersion"]` (or
  `server/discover`) is modern; `initialize` starts a legacy session. On HTTP, a legacy request must carry a
  valid `Mcp-Session-Id` after `initialize`. A modern request must not carry one.
- One method table (tools, resources, prompts) serves both eras. Only the envelope, the handshake, the
  notification routes and the cancellation differ. These differences live in two small adapters
  (`LegacyDialect`, `ModernDialect`).
- **Legacy HTTP sessions:** at most 16, with an idle expiry of 30 min. DELETE ends a session. GET opens the
  session's notification stream (one per session; used for resource updates).
- **Modern:** results carry `resultType`, and list results carry `ttlMs` and `cacheScope`. Notifications use
  `subscriptions/listen`.
- No MRTR or elicitation is used in v1 (consent is local, ADR-0101). The tasks extension is not offered (ADR-0107).

## Consequences

- Every protocol test runs in both eras. Interop is checked manually with Claude Code (modern and legacy if
  selectable), Cursor (legacy) and the official conformance suite (0013 verification).
- When all major clients speak 2026-07-28, a later ADR may drop the legacy dialect.
