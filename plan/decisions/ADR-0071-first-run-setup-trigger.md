# ADR-0071 — When the first-run setup wizard opens

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M4 planning session)

## Context

Without J-Link Software the app cannot do anything with a device. New users should be guided; experienced users
must not be nagged; people who only want to look around must be able to skip.

## Options considered

1. **First start, and every start where the J-Link library check fails; skippable (chosen).**
2. First start only.
3. Never automatically.

## Decision

- The wizard opens automatically when the main window is shown and:
  - the setting `setup/completedVersion` is absent (first start), or
  - the J-Link library check (located, loadable, version ≥ 8.00) fails and `setup/showAtStart` is true.
- [Skip setup] and [Finish] both set `setup/completedVersion` to the app version.
- When it opens because J-Link is missing (not the first start), it shows the checkbox "Don't show at start"
  (`setup/showAtStart = false`).
- nRF Util is optional and never triggers the wizard.
- The wizard is always reachable from Help → "Setup wizard…" and from "Help me fix this" on `dependency.*` errors.
- The start-up check runs on a worker; the window is shown first and the wizard opens when the check finishes.

## Consequences

- New settings keys `setup/completedVersion` (string) and `setup/showAtStart` (bool, default true).
