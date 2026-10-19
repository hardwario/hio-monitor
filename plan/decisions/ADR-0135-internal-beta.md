# ADR-0135 — Internal beta: staff release candidates with criteria-based exit

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M8 planning session)

## Context

Before v2.0.0 replaces v1 for all users, v2 must be used for real work. Public betas were considered; the user chose an
internal beta.

## Options considered

1. **Staff release candidates, criteria-based exit (chosen).**
2. Fixed-length beta.
3. One RC sign-off.
(Public pre-releases and a closed customer beta were rejected.)

## Decision

- Release candidates are tags `v2.0.0-rc.N` built by the release workflow (ADR-0125) and published as GitHub
  **pre-releases** (never "latest", so v1's update check and v2's default channel ignore them).
- Testers: HARDWARIO staff from support, firmware development and production, named in 0018 (at least one per OS in the
  ADR-0012 matrix). They use v2 instead of v1 and py-hardwario for daily work.
- Feedback: GitHub issues with the templates "Bug (v2 RC)" and "Parity gap", each asking for the diagnostic report
  (ADR-0075); labels `rc-blocker`, `crash`, `parity`.
- **Exit criteria** (all): ≥ 2 weeks of use on the final RC; 0 open `rc-blocker` and `crash` issues; every row of the
  0002 parity checklist ticked; 0015's manual matrix passed on the final RC; HIL workflow green on the final RC tag
  (ADR-0140); the ADR-0119 legal sign-off done; 0 unfinished `cs` translations.
- A new RC is cut for every fix; the 2-week clock restarts only for fixes of `rc-blocker` or `crash` issues.

## Consequences

- No external user sees v2 before v2.0.0; field coverage depends on staff hardware variety.
