# ADR-0013 — Plan-first process and Git LFS for binaries

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (bootstrap planning session)

## Context

The project is long-lived and developed with AI agents.

## Decision

Every feature goes through an approved plan in `plan/` first and is then implemented by a separate agent. Important decisions are asked of the user and recorded as ADRs. Large/binary files are committed only through **Git LFS**; vendor SDK binaries are never committed.

## Consequences

- See CLAUDE.md and plan/README.md.
