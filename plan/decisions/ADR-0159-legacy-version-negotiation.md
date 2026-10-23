# ADR-0159 — Legacy `initialize` with an unknown version is answered with 2025-11-25 (amends ADR-0095)

- **Status:** Accepted
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (M6, after the independent review of 0013 phases A–C)

## Context

ADR-0095 says an unsupported protocol version gets the version error. The 2025-11-25 lifecycle rules require a
server to answer `initialize` with a version it supports when it does not support the requested one; the client then
decides whether to continue.

## Decision

A legacy `initialize` with any version we do not support is answered with `2025-11-25`, as the spec requires. Modern
requests (2026-07-28 `_meta` protocol version or `server/discover`) with an unknown version still get
`UnsupportedProtocolVersion` (-32022) with the supported list. ADR-0095 gets an "amended by ADR-0159" note.

## Consequences

- Spec-conformant handshake for older and future legacy clients; checked in the conformance run (E1).
