# 0018 — M8 internal beta, v2.0.0 release and retirement of the old app

- **Status:** Draft
- **Milestone:** M8 (plan 2 of 2, ADR-0131; after [0017](0017-m8-parity.md))
- **Author:** Matej Strnad (with Claude), 2026-10-19
- **Related ADRs:** ADR-0012, ADR-0014, ADR-0018, ADR-0075, ADR-0116, ADR-0117, ADR-0119, ADR-0125, ADR-0131,
  ADR-0133–ADR-0136, ADR-0140
- **Depends on:** 0015 and 0016 Done (release pipeline, update check); 0017 Done before the exit review (phase C).

## Context

After M8's parity work, v2 must prove itself in daily use and then replace v1 for everyone. v1 users have v1's update
check, which points them to whatever GitHub release is "latest" (ADR-0136). The repository still has the old app on
`main` and v2 on the orphan `v2-dev` (ADR-0014). This plan is mostly process: release candidates, feedback handling,
exit review, the v2.0.0 release and the branch switch. Each outward-facing step (publishing a release, moving `main`,
closing v1 issues) is done only after the user's explicit approval at that moment.

## Goals / Non-goals

**Goals**
1. Release candidates `v2.0.0-rc.N` for HARDWARIO staff with issue templates and labels (ADR-0135).
2. A documented exit review against the ADR-0135 criteria.
3. v2.0.0 published as latest with "Coming from v1" release notes (ADR-0136).
4. Branch switch: `v1-final` tag, `v1` branch, `main` = v2, CI and protections moved (ADR-0133, ADR-0134).
5. Old-app retirement housekeeping: v1 README notice, v1 issues triaged, roadmap/vision updated.

**Non-goals**
- Public or customer beta (ADR-0135); py-hardwario deprecation (ADR-0137); docs.hardwario.com publishing (open question
  29); history rewrite (ADR-0134); post-2.0 features.

## User experience

- **Staff tester:** gets a link to the RC pre-release on GitHub, installs it next to v1, reports issues via "Bug
  (v2 RC)" or "Parity gap" templates (both ask for OS, package, steps, and attach the diagnostic report from Help →
  "Create diagnostic report…"). v2's own update check offers newer RCs because the running version is a pre-release
  (0016 default).
- **v1 user after v2.0.0:** v1's update dialog → Download → v2.0.0 release page → "Coming from HARDWARIO Monitor v1"
  section at the top → installs v2 → first-run setup (0008) guides J-Link Software installation.
- **Developer with an old clone:** README and release notes explain `git fetch && git checkout main && git reset --hard
  origin/main`.

## CLI

None.

## Design

### A. RC infrastructure

- `.github/ISSUE_TEMPLATE/bug-v2-rc.yml`, `parity-gap.yml` (forms: OS + version, package kind, steps, expected,
  actual, diagnostic report upload, severity). Labels `rc-blocker`, `crash`, `parity`, `v2-rc`.
- `packaging/RELEASE.md` (0015) gets the RC section: cut `v2.0.0-rc.N` from `v2-dev`, release workflow + HIL workflow
  (ADR-0140) run, publish as pre-release (never latest), announce to testers.
- Tester list (names, OS, hardware) kept in the HARDWARIO internal tracker, not in the repository; this plan names the
  roles: support, firmware development, production — at least one tester per OS of ADR-0012.

### B. RC cycle

- Weekly triage of `v2-rc` issues; every fix goes to `v2-dev` through normal PRs; a new RC is cut when fixes land.
- The 2-week clock (ADR-0135) restarts on fixes of `rc-blocker` / `crash` issues only.

### C. Exit review

A checklist issue "v2.0.0 release readiness" with one item per ADR-0135 criterion, each linking evidence:
2 weeks on final RC; 0 open `rc-blocker`/`crash`; 0002 parity table complete (0017); 0015 manual matrix on the final
RC; HIL green on the final RC tag; ADR-0119 legal sign-off; `check_translations` 0 unfinished. The user approves the
checklist before tagging `v2.0.0`.

### D. v2.0.0 release

- Tag `v2.0.0` on the final RC commit (no code change between final RC and release).
- Release workflow → draft; release notes: "Coming from HARDWARIO Monitor v1" (ADR-0136 content), what's new,
  minimum OS versions, checksums/attestation verification instructions, known issues.
- Publish as **latest** — after the user's explicit approval.
- Verify: v1 on each OS shows its update dialog pointing to v2.0.0 (v1's `!=` check, ADR-0136).

### E. Branch switch (ADR-0133) — after v2.0.0 is published, with approval at each step

1. Annotated tag `v1-final` on the current `main`; branch `v1` from it; protect `v1`.
2. Close/retarget open PRs against `main`; announce the switch.
3. Update CI on `v2-dev` first: `ci.yml` triggers `main` instead of `v2-dev`; `hil.yml`/`release.yml` unchanged
   (tags); README clone instructions (ADR-0134).
4. Force-move `main` to `v2-dev`'s tip; move branch protection rules to `main`.
5. `v1` README notice commit ("retired, see main for v2").
6. After the next v2 release (one cycle): delete `v2-dev`.

### F. Retirement housekeeping

- v1 issues: label `v1`, close with a template comment pointing to v2 (bulk action after approval).
- `plan/roadmap.md`: M8 Done; `plan/0000-vision.md` success criteria marked met; ADR-0014 status "Superseded by
  ADR-0133" for the branch part.
- Old CI workflows on `v1` are left disabled (they trigger only on release creation and would use Qt 6.6).

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| Beta | public / closed / internal | internal: staff RCs, criteria-based exit | ADR-0135 |
| v1 users | latest + notes / final v1 release / suppress | v2.0.0 as latest + "Coming from v1" notes | ADR-0136 |
| Branches | v2-dev → main / switch default / merge | `v1-final` + `v1`, `main` moved to v2 | ADR-0133 |
| History rewrite | none / archive repo / filter-repo | none | ADR-0134 |
| HIL runner down at release time | block the release / documented manual HIL run signed off by the user | manual HIL run (suite from `hil/README.md` on a dev machine), results attached, explicit user sign-off in the readiness checklist | — |
| Minor defaults: issue templates and labels above; tester list outside the repo; weekly triage; no code change between final RC and v2.0.0; `v2-dev` deleted after one release cycle; v1 issues bulk-closed with a pointer; v1 CI left disabled | accept / change | accepted (2026-10-19) | — |

## Implementation steps

- [ ] A1 Issue templates, labels, RELEASE.md RC section.
- [ ] A2 First RC `v2.0.0-rc.1` (user approves publishing), testers notified.
- [ ] B1 RC cycle until exit criteria hold (track RC numbers and dates here).
- [ ] C1 Release readiness checklist issue complete; user approval.
- [ ] D1 Tag `v2.0.0`, draft release with notes; user approval → publish as latest; v1 update-dialog check.
- [ ] E1–E6 Branch switch steps (each approved).
- [ ] F1 Housekeeping (v1 issues, roadmap, vision, ADR-0014 status).

## Tests and verification

- Release workflow and HIL workflow green on every RC and on `v2.0.0`.
- Downloaded assets verified against `SHA256SUMS` and `gh attestation verify` on each OS.
- v1 → v2 path walked on Linux (AppImage v1 → .deb or AppImage v2), macOS and Windows: v1 dialog, download, install,
  first-run setup, RTT console with a CHESTER.
- After the branch switch: fresh clone of `main` builds with the documented commands; `v1` builds nothing in CI.

## Risks and open points

- Staff hardware/OS variety may not cover all customers' setups (e.g. older Windows 10 builds) — the manual matrix of
  0015 is the safety net.
- Moving `main` breaks existing clones and forks' tracking; mitigated by announcement and README.
- v1's update dialog quits v1 after Download (old behaviour); users may be surprised — mentioned in the notes.
- Legal sign-off (open question 28) and Azure Trusted Signing eligibility (0015) are external blockers.
