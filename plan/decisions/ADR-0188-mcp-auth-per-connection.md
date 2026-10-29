# ADR-0188 — MCP auth failures are limited per connection (supersedes the lockout of ADR-0102)

- **Status:** Accepted
- **Date:** 2026-10-29
- **Decided by:** Matej Strnad

## Context
ADR-0102: 10 failures in 60 s lock every client out for 10 s. Review M-8: any local process can keep the lock
active and deny service to the legitimate client; with a 256-bit token a global limiter adds no security.

## Options considered
1. Per-connection budget plus constant delay. 2. No limiter, delay only.

## Decision
Option 1. A failed auth gets 401 after a constant 250 ms (timer, owner thread not blocked) and the connection is
closed, as every refusal already closes it (budget = 1 failure per connection; corrected 2026-10-29 from "3"). No process-global state. A request with the correct token is never refused for auth reasons.

## Consequences
Brute force stays infeasible (token entropy); local DoS via lockout disappears. Connection limit (16) still applies.
