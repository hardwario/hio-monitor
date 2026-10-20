# ADR-0096 — Own MCP implementation on QtNetwork + QJsonDocument

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (M6 planning session)

## Context

There is no official C++ MCP SDK. The community libraries (cpp-mcp: only 2025-03-26; gopher-mcp: libevent +
OpenSSL; cxxmcp: cpp-httplib with its own threads) each bring a second HTTP stack, a second JSON library, their own
threads or event loop, and exceptions in their APIs. Qt HTTP Server is GPLv3/commercial, not LGPL, so ADR-0002
rules it out.

## Options considered

1. **Own implementation (chosen).**
2. cxxmcp (MIT, all revisions).
3. gopher-mcp (Apache-2.0).

## Decision

- `libs/mcp` (QtCore + QtNetwork only, ADR-0004) contains:
  - a transport-neutral JSON-RPC 2.0 + MCP core on `QJsonDocument` (ADR-0019);
  - a minimal, bounded **HTTP/1.1 server** on `QTcpServer`/`QTcpSocket`: request line, headers, Content-Length
    bodies (no chunked requests), keep-alive, JSON or `text/event-stream` responses;
  - a **stdio transport** with newline-delimited JSON.
- No new third-party dependency.
- The official MCP conformance suite and a reference client are used **only as test tools** (manual or optional
  CI job), never linked.

## Consequences

- Full control of limits, Origin and token checks, error mapping to `hio::Error`, and threading (ADR-0097).
- We own the HTTP parser. It gets strict limits (ADR-0102), unit tests with malformed input, and a libFuzzer
  target in the ASan preset.
- Spec updates are our job. `libs/mcp` keeps the protocol strings and constants in one header.
