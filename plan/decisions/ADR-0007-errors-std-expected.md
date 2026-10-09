# ADR-0007 — Error handling with std::expected

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (bootstrap planning session)

## Context

The old app failed silently or showed raw codes. Errors must be structural and actionable. Exceptions are hard to guarantee across Qt signals, threads and C callbacks.

## Decision

Use `hio::Result<T> = std::expected<T, hio::Error>` throughout the backend. `hio::Error` carries code, user message, technical detail, suggested fix and optional wizard id. No exceptions cross module, thread or C-callback boundaries; third-party exceptions are caught at the adapter boundary and converted.

## Consequences

- All such functions are `[[nodiscard]]`.
- UI can always show what happened, why and what to do.
