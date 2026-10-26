# 0023 — CI repair: all `ci.yml` jobs green

- **Status:** In progress (approved 2026-10-26)
- **Milestone:** maintenance (blocks M7 packaging jobs, plan 0015)
- **Author:** Matej Strnad (with Claude), 2026-10-26
- **Related ADRs:** ADR-0179 (macOS 13.3, amends ADR-0012), ADR-0001, ADR-0002, ADR-0010, ADR-0023
- **Depends on:** none. Branch `strnad/ci-repair-90059f`, cut from local `v2-dev` 499411b.

## Context

`.github/workflows/ci.yml` has been red on every job except `lint` since it first ran
(run [39434839298](https://github.com/hardwario/hio-monitor/actions/runs/39434839298); the same on
`v2/m7-packaging` run 40337845727). Nobody noticed because the presets pass on the developers'
Linux host, which uses the distro Qt 6.10 instead of the Qt 6.8.3 that CI installs. The failures:

| Job | Failure |
|---|---|
| `macos-*` | Configure/compile: `std::format` unavailable at deployment target 13.0 |
| `linux-tidy` | `misc-include-cleaner` wants `<QStringLiteral>` (~170 files) with Qt 6.8.3; generated rcc init files are linted |
| `linux-debug`, `linux-tsan` | `CliProcess.CzechLocaleStaysEnglish`: the runner has no `cs_CZ.UTF-8` locale |
| `linux-tsan` | every `tst_gui.*` Qt Quick test fails |
| `windows-*` | never compiled: MSVC STL array iterators, Linux-only helpers, `getenv` deprecation, … |

`package-macos` / `package-windows` on `v2/m7-packaging` need this matrix green first.

## Goals / Non-goals

- Goal: every `ci.yml` job passes (lint, linux ×6, fuzz, macos ×3, windows ×2).
- Goal: the local gates still pass (`linux-debug` ctest, `linux-tidy` zero findings, clang-format,
  `all_qmllint`, `check_translations`), now also reproducible with Qt 6.8.3.
- Non-goal: new features, packaging (plan 0015), HIL runs, making the TSan flakes of plans 0019/0020
  part of this plan (they stay on their branches; if the CI failure turns out to be the same report,
  this plan says so and the fix is coordinated, not duplicated).

## User experience

None, except: the supported macOS minimum becomes 13.3 (ADR-0179).

## CLI

None.

## Design

1. **macOS 13.3** (ADR-0179): `CMAKE_OSX_DEPLOYMENT_TARGET` 13.3 in `CMakePresets.json`; ADR-0012
   status line, plan 0001 (preset description, Apple libc++ risk), 0011/0012 ("macOS 13+" → 13.3+)
   updated. Plan 0015 lives on `v2/m7-packaging` and is **not** edited here (follow-up there).
2. **clang-tidy with Qt 6.8.3:** add the missing Qt includes `misc-include-cleaner` asks for
   (scripted, then clang-format); exclude generated sources (rcc `qrc_*.cpp` / resource init files,
   moc, qmltyperegistrar output in the build tree) from tidy with CMake's `SKIP_LINTING` source
   property, the mechanism `HioBackendRules.cmake` already uses for generated-only targets. Verified
   with both Qt 6.8.3 (aqtinstall, as CI) and the host Qt 6.10: the includes must not trigger
   "unused include" findings on 6.10.
3. **Runner locale:** the Linux CI jobs generate `cs_CZ.UTF-8` (`locale-gen`) before testing; the
   test is not weakened. If the locale is missing on a developer machine the test keeps failing
   with its current message (unchanged behaviour).
4. **TSan Qt Quick tests:** reproduce `ctest --preset linux-tsan -R tst_gui` with Qt 6.8.3 (the
   official binaries are uninstrumented as on the host, but built differently from the distro Qt),
   find the cause, fix it. A suppression is used only for a proven false positive inside
   uninstrumented Qt/glib code, documented in `tools/sanitizers/` with the analysis, the same policy
   as plans 0019/0020.
5. **Windows clang-cl:** cherry-pick `afa42dc` (MSVC STL array iterators) and `57e0008` (Linux-only
   USB helpers `[[maybe_unused]]`) from `v2/m7-packaging`, then fix the remaining errors one by one
   (first known: `getenv` deprecation in `tests/fake_nrfutil/fake_nrfutil.cpp`) and the Windows
   test failures. Fixes stay within the code rules (no `_CRT_SECURE_NO_WARNINGS` globally; use
   portable code or `qEnvironmentVariable` where Qt is available). There is no local Windows host:
   iterate on CI.
6. **macOS:** after (1), fix whatever the `macos-15` build/tests show (iterate on CI).

Each fix is a separate commit. Any fix that needs an architectural or otherwise important change
(new dependency, interface change, test policy) stops and is asked first.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| macOS minimum | 13.0 + workaround / **13.3** | 13.3 (2026-10-26) | ADR-0179 |
| Plan/ADR numbers | 0022 / ADR-0178 are taken on `v2/console-help-gate-plan`; 0019/0020 by the TSan branches | 0023 / ADR-0179 (2026-10-26) | — |
| Missing `cs_CZ` locale | **generate in CI** / skip test when missing | generate in CI (session brief) | — |
| How CI runs for this branch (no `gh` CLI here; `origin/v2-dev` is 6 commits behind local `v2-dev`) | **user opens a PR** from the pushed branch to `v2-dev` / temporary `push` trigger for this branch in `ci.yml`, removed before merge | temporary push trigger (2026-10-26) | — |
| TSan Qt Quick failure fix | depends on the cause found | _pending (asked when known, if not a plain bug fix)_ | — |

## Implementation steps

- [ ] A. ADR-0179, presets to 13.3, docs/plans updated
- [ ] B. Qt 6.8.3 tidy: generated sources excluded, Qt includes added; zero findings on 6.8.3 and 6.10
- [ ] C. `cs_CZ.UTF-8` generated in the Linux CI jobs
- [ ] D. TSan `tst_gui` failures reproduced, cause fixed (documented)
- [ ] E. Windows: cherry-picks + remaining compile and test fixes
- [ ] F. macOS: build and tests pass on `macos-15`
- [ ] G. All `ci.yml` jobs green on the branch; temporary trigger (if any) removed

## Tests and verification

- Local, host Qt 6.10: `ctest --preset linux-debug`, `linux-tidy` build with zero findings,
  clang-format, `all_qmllint`, `check_translations`.
- Local, Qt 6.8.3 (`QT_ROOT_DIR` to the aqtinstall tree): `linux-tidy` zero findings,
  `ctest --preset linux-tsan -R tst_gui`.
- GitHub CI: one run where all jobs pass; run URLs recorded here.

## Risks and open points

- Windows and macOS can only be tested on CI (slow iterations, ~1 h per round).
- Follow-up on `v2/m7-packaging` after merge: plan 0015 `LSMinimumSystemVersion` 13.0 → 13.3, drop
  its temporary push trigger.
