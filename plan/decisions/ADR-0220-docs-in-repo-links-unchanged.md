# ADR-0220 — User documentation stays in `docs/`; in-app links unchanged until docs.hardwario.com

- **Status:** Accepted (confirms the location part of ADR-0138)
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad (plan 0029 session)

## Context

Plan 0028 expected plan 0029 to target docs.hardwario.com and to supersede ADR-0138's GitHub `docs/` target. The
sibling project tower-bridge keeps its documentation in the repository and links into it from the README.

## Options considered

1. **`docs/` in the repository, rendered by GitHub; docs.hardwario.com publishing decided later (chosen).**
2. `docs/` and publishing to docs.hardwario.com in plan 0029.
3. docs.hardwario.com only.

For the in-app links (`hwio::docsUrl()`: Help → Documentation, Console → Batch):

1. **Unchanged: the docs.hardwario.com HARDWARIO Monitor page until the docs are published there (chosen).**
2. Point to the GitHub `docs/<lang>/` pages now.

## Decision

- `docs/` is the source of truth (ADR-0138 kept); open question 29 (publishing on docs.hardwario.com) stays open.
- Plan 0029 changes no code: `libs/core/src/docs_urls.cpp` keeps pointing to the docs.hardwario.com Monitor page.
- Absolute links to the GitHub repository appear only where a relative link cannot work (README badges, release
  downloads) and use one Markdown reference definition per file, so a repository move changes one line per file.

## Consequences

- The app does not link to the new pages yet; the `docsUrl()` table changes when the docs are published.
- The repository (`hardwario/monitor`, the new name) renders the docs without any site tooling.
