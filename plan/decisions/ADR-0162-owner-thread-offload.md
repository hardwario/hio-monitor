# ADR-0162 — Blocking work of MCP handlers runs on short-lived worker threads

- **Status:** Accepted
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (M6, after the independent review of 0013 phases A–C)

## Context

ADR-0097 says tool handlers on the owner thread never block. The review found blocking work: the Intel HEX parse of
images up to 64 MiB (`flash`), the LTE zip classification (`flash_lte`) and the probe enumeration
(`CliDeviceProvider::refresh`).

## Decision

Such work runs on an owned `std::jthread` per job (at most 4 at once per server; more → the call waits in a bounded
queue of 16, beyond that `mcp.too_many_requests`). The job gets a `stop_token` (cancelled with the call), works only on
its own data, and posts the result back to the owner thread with a queued call guarded by a `QPointer`. Session and
`ConsoleLog` objects are never touched from the job thread. The threads are joined when the server stops.

## Consequences

- The GUI stays responsive while an image is parsed. TSan covers the offload helper.
