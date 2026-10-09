# ADR-0008 — MCP server over HTTP (GUI) and stdio (CLI)

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (bootstrap planning session)

## Context

The app must integrate an MCP server for AI assistants.

## Decision

The GUI hosts MCP over **Streamable HTTP on 127.0.0.1** with a token (opt-in), sharing the live session. A headless CLI serves MCP over **stdio**. Both use `libs/mcp`.

## Consequences

- AI can observe and act on the user's live session, or run headless in CI.
- Security design (token, origin checks, consent for destructive tools) required — plan/0004.
