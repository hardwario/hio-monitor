# ADR-0134 — No history rewrite to remove the v1 vendor binaries

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M8 planning session)

## Context

The old app committed ~380 MB of J-Link and nrfjprog binaries (0002). ADR-0014 left a history rewrite as a separate
decision.

## Options considered

1. **No rewrite (chosen).**
2. Move v1 to an archive repository and drop it here.
3. `git filter-repo` across all history.

## Decision

History stays intact. The binaries stay reachable through `v1`, `v1-final` and the `v1.*` tags. The README documents a
small clone for v2 work: `git clone --single-branch --branch main https://github.com/hardwario/hio-monitor` (or
`--filter=blob:none`).

## Consequences

- Existing clones, forks, tags and links remain valid.
- Full clones stay large; CI uses shallow/single-branch checkouts except where tags are needed (release, ADR-0125).
