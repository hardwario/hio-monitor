# ADR-0014 — Rewrite lives on orphan branch v2-dev

- **Status:** Accepted (amended by ADR-0133: at retirement `main` is moved to v2 and v1 is kept as branch `v1` / tag `v1-final`)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (bootstrap planning session)

## Context

The rewrite shares no code with the old app.

## Decision

Develop v2 on an orphan branch **`v2-dev`** that initially contains only the copied LICENSE and new files. `main` keeps the old app until v2 replaces it (M8).

## Consequences

- No old binaries in v2 history; the repository clone still contains them via `main` (history rewrite is a separate decision).
