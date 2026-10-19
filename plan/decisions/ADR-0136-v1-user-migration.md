# ADR-0136 — v1 users are migrated by publishing v2.0.0 as the latest release

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M8 planning session)

## Context

v1's update check fetches `releases/latest` and shows a dialog whenever `tag_name` differs from its own version
(0002, string `!=`), with a Download button to the latest release page. v1 and v2 releases share the repository
(ADR-0125).

## Options considered

1. **Publish v2.0.0 as "latest" plus a "Coming from v1" section in the release notes (chosen).**
2. Ship a final v1.3.0 with an in-app notice first.
3. Avoid notifying v1 users.

## Decision

- v2.0.0 is published as the **latest** release; every v1 installation shows its update dialog on the next start and
  opens the v2.0.0 release page.
- The release notes start with "Coming from HARDWARIO Monitor v1": v2 installs side by side (new identifiers,
  ADR-0018/0117); install SEGGER J-Link Software ≥ 8.00 (v2 no longer bundles it, ADR-0006); settings and command
  history are not migrated; uninstall v1 when satisfied (per-OS steps); `hio` replaces the `hardwario` CLI for
  scripts (link to the compatibility table, ADR-0139).
- No further v1 release. v1 issues are closed with a pointer to v2 after v2.0.0.
- Pre-releases (ADR-0135) are never "latest", so v1 users are not pointed at RCs.

## Consequences

- Later v2 releases also appear as "latest" to v1 users, which is intended.
- If v2.0.0 has a severe regression, the fix is v2.0.1 (no rollback of "latest" to v1).
