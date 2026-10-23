# ADR-0102 — MCP HTTP security: 127.0.0.1, port 47800, bearer token file, Origin checks, limits

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (M6 planning session)

## Context

ADR-0008 requires localhost, a token and Origin validation (DNS-rebinding protection). MCP authorization is
optional, and OAuth is not required for a local server. Both spec revisions require Origin validation (403 for an
invalid Origin).

## Decision

- **Bind** to 127.0.0.1 only (no `::1`, no other interfaces; not configurable in v1). The port defaults to
  **47800** and can be set to 1024–65535. If the port is taken, the server does not start
  (`mcp.port_in_use`); it never moves to another port silently.
- **Token:** 32 random bytes (from `QRandomGenerator::system()`), base64url, 43 characters. It is stored in
  `<AppConfigLocation>/mcp-token`:
  - POSIX: mode 0600 in a 0700 directory, checked on every load; wrong modes are fixed and a warning is logged.
  - Windows: owner-only ACL.
  - It is created when the server is first enabled, regenerated on request, and never written to the INI file,
    the logs or diagnostic reports (the report generator filters it out).
- **Auth:** every request needs `Authorization: Bearer <token>`, compared in constant time. A missing or wrong
  token gives 401 with `WWW-Authenticate: Bearer`. After 10 failures in 60 s, every request from any client gets
  429 for 10 s.
- **Origin:** a request with an `Origin` header is accepted only when it is `http://127.0.0.1:<port>` or
  `http://localhost:<port>`; anything else gets 403. A request without Origin is accepted (non-browser clients).
  `Host` must be `127.0.0.1:<port>` or `localhost:<port>`, otherwise 421. No CORS headers are sent.
- **Limits:** 16 connections; request line + headers 16 KiB; body 4 MiB; header read timeout 10 s; body timeout
  30 s; idle keep-alive 60 s; SSE streams limited by the in-flight call limit (ADR-0097) + 16 listen streams; at
  most 1 MiB queued per connection (otherwise the slow client is disconnected and its calls are cancelled).
- **stdio:** no token (the client spawned the process). Lines ≤ 4 MiB; a longer line gives a JSON-RPC parse error
  and is discarded up to the next newline.

## Consequences

- No TLS: localhost only. A remote use case would need a new ADR (TLS + OAuth).
- Clients configure the token as a header; 0014 offers ready-made snippets (ADR-0106).
