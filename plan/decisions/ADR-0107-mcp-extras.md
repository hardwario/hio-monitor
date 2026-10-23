# ADR-0107 — MCP v1 extras: `debug_device` prompt, progress and cancellation, log/status resources; no tasks

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (M6 planning session)

## Context

Besides tools, MCP offers prompts, resources (with subscriptions), progress notifications, cancellation and the
tasks extension. rttt offers one prompt (`debug_device`).

## Decision

- **Prompt `debug_device`:** the rttt workflow text adapted to our tools, CHESTER targets (`app`/`lte`), device
  ids, consent errors and the rttt tool-name mapping (`jlink_close` → `release_probe`).
- **Progress:** when a request carries a `progressToken`, `flash`, `flash_lte`, `erase`, `recover`,
  `write_flash`, `diagnose`, `doctor`, `wait_for_state` and `wait_for_connection` send
  `notifications/progress`, at most 4 per second (HTTP: on the request's SSE stream; stdio: interleaved
  notifications).
- **Cancellation:** stdio `notifications/cancelled`; HTTP closing the request stream (modern) or
  `notifications/cancelled` (legacy). It maps to `DeviceSession::cancel` with the M2 granularity (the catalog
  download is not interruptible; ADR-0035 rules).
- **Resources:** templates `hio://device/{id}/status` (JSON), `hio://device/{id}/log` and
  `hio://device/{id}/terminal` (text, last 1000 lines), and `hio://devices` (JSON list). Subscriptions
  (legacy `resources/subscribe` + GET stream; modern `subscriptions/listen`) send
  `notifications/resources/updated` at most once per second per resource; `resources/list_changed` is sent on
  device open/close. There are at most 64 subscriptions per server.
- **Not in v1:** the tasks extension, sampling, elicitation/MRTR, completion, roots, logging.

## Consequences

- Long operations show progress in clients that support it. The others still get the final result.
