# ADR-0133 — At retirement, v2-dev becomes main; v1 is kept as branch v1 and tag v1-final

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M8 planning session)

## Context

ADR-0014: v2 lives on the orphan branch `v2-dev`; `main` keeps the old app until M8. After v2.0.0 the repository's
default branch must hold v2.

## Options considered

1. **Tag and keep v1, then point `main` at v2-dev (chosen).**
2. Make a renamed v2-dev the default branch; leave `main` as the old app.
3. Merge the orphan history into `main` with `--allow-unrelated-histories`.

## Decision

Done after v2.0.0 is published (0018), each step with the user's explicit approval at that time:
1. Tag the last v1 commit `v1-final` (annotated) and create branch **`v1`** from it; protect `v1` (no pushes except
   maintainers, no force push). v1 release tags (`v1.2.x`) stay.
2. Move **`main`** to the tip of `v2-dev` (a force update of `main`, the one sanctioned history rewrite of a shared
   branch). Open PRs against `main` are closed or retargeted beforehand.
3. Default branch stays `main`; CI triggers and branch protection rules move from `v2-dev` to `main`; `v2-dev` is
   deleted after one release cycle (kept read-only until then so open clones can rebase).
4. README of `v1` says "This is the retired HARDWARIO Monitor v1. See main for v2."
5. Feature branches `v2/*` used during development are deleted after merge, as usual.

## Consequences

- Clones that tracked `main` must reset (`git fetch && git reset --hard origin/main` or re-clone); announced in the
  release notes and README.
- `ci.yml`/`release.yml` triggers are updated in the same change (0018).
