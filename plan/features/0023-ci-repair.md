# 0023 — CI repair: all `ci.yml` jobs green

- **Status:** In progress (approved 2026-10-26)
- **Milestone:** maintenance (blocks M7 packaging jobs, plan 0015)
- **Author:** Matej Strnad (with Claude), 2026-10-26
- **Related ADRs:** ADR-0179 (macOS 13.3, amends ADR-0012), ADR-0180 (Apple clang `-fexperimental-library`), ADR-0177, ADR-0001, ADR-0002, ADR-0010, ADR-0023
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

### Findings during implementation (2026-10-26)

Job logs need a GitHub sign-in; annotations do not. The branch therefore carries the problem
matchers of `v2/m7-packaging` (permanent) and a TEMPORARY failure-notes step
(`.github/ci-failure-notes.py`: failed test output and failed build edges as `::notice`
annotations), removed before the merge together with the push trigger.

| Area | Cause | Fix |
|---|---|---|
| Linux / Windows build | `std::expected<ToolResult>` meta type: libstdc++ < 15 and MSVC STL instantiate `operator==` | ADR-0177 (cherry-picked from `v2/m7-packaging` 61fc854 / 44efd03) |
| macOS configure | `std::format` needs deployment target 13.3 | ADR-0179 |
| macOS configure | Xcode 16 libc++: `std::jthread` / `stop_token` only with `-fexperimental-library` | ADR-0180 |
| macOS build | `recordedAppName()` unused on macOS | `[[maybe_unused]]` |
| `linux-tidy` (Qt 6.8.3) | include-cleaner wants `<QStringLiteral>` (and `<QChar>`, `<QLatin1Char>`); `QPointer` converts implicitly to `T*` (Qt 6.10: explicit `operator bool`); Qt 6.8 does not mark generated QML sources `SKIP_LINTING` | includes; `isNull()`; `hio_skip_linting_generated_sources()` and the `qt_add_qml_module` `OUTPUT_TARGETS` through `hio_skip_linting()` |
| `linux-tsan` | glib wakeup-fd false positive fires on every `tst_gui` run with Qt 6.8.3; `race:tzset_internal` cannot match the stripped runner glibc | plans 0019 + 0020 brought in; `libc6-dbg` in the TSan job |
| `linux-*` | no `cs_CZ.UTF-8` on the runner | `locale-gen` in the Linux jobs |
| `linux-release` | `FlashTest.CatalogDownloadAndErrors` reached the real catalog (Release ignores `HIO_CATALOG_URL`); a scan test raced the 5 ms `waitFor()` slices | skip without `HIO_TEST_OVERRIDES`; step one event-loop pass at a time |
| Windows build | MSVC STL array iterators; Linux-only helpers; `getenv` deprecation; `fs::path` → `std::wstring` | afa42dc, 57e0008 (cherry-picked); `_CRT_SECURE_NO_WARNINGS` for the fake nRF Util only; `.string()` |
| Windows tests | text-mode `\r\n`; empty `PATH` hid the Qt DLLs (0xC0000135); `QStandardPaths` ignores the child's `LOCALAPPDATA` | test-side normalization; Qt `bin` on `PATH`; check the log file the message names |
| Windows tests (round 2) | helper / MCP banner `\r\n`; recent list keeps `/` separators; `/nonexistent` is relative without a drive; a Windows-only lock test took its own lock; `QLocalServer` names are pipes, not files | normalization; `fromNativeSeparators`; absolute path from the test root; contents computed first; two socket-file tests Unix only |
| macOS tests | `~SignalGuard` restored the default SIGPIPE action before exit, the C library's final stdout flush then killed `hio … \| head` (141); `detect_leaks` is fatal with ASan on Apple silicon; `QStandardPaths` ignores `HOME` / `XDG_*`; `.so` names; the real Bluetooth permission (TCC) answers instead of the fake scenario | SIGPIPE stays ignored; macOS ASan test preset without `detect_leaks`; Linux-only path checks and tests (with the reason); two permission tests skipped on macOS (with the reason) |
| macOS + Windows | the firmware cache is in the system folders, so `CatalogNetworkDown` found `FlashByCatalogId`'s download | an ID that is never downloaded |
| `linux-coverage` | `tst_gui` moved to `app/gui/tests/` | path in the coverage step |

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| macOS minimum | 13.0 + workaround / **13.3** | 13.3 (2026-10-26) | ADR-0179 |
| Plan/ADR numbers | 0022 / ADR-0178 are taken on `v2/console-help-gate-plan`; 0019/0020 by the TSan branches | 0023 / ADR-0179 (2026-10-26) | — |
| Missing `cs_CZ` locale | **generate in CI** / skip test when missing | generate in CI (session brief) | — |
| How CI runs for this branch (no `gh` CLI here; `origin/v2-dev` is 6 commits behind local `v2-dev`) | **user opens a PR** from the pushed branch to `v2-dev` / temporary `push` trigger for this branch in `ci.yml`, removed before merge | temporary push trigger (2026-10-26) | — |
| TSan Qt Quick failure fix | depends on the cause found | the glib wakeup-fd false positive of plan 0020 (deterministic with Qt 6.8.3): bring plan 0019 (cherry-pick 9a979f7) and plan 0020 A2 (uncommitted in worktree kind-mcclintock-5da9a4) into this branch (2026-10-26) | — (plans 0019/0020) |
| `std::jthread` on macOS: every Xcode 16 on `macos-15` needs `-fexperimental-library` | **flag for Apple clang** / Homebrew LLVM 21 / own jthread | flag for Apple clang (2026-10-26) | ADR-0180 |

## Implementation steps

- [x] A. ADR-0179, presets to 13.3, docs/plans updated
- [ ] B. Qt 6.8.3 tidy: generated sources excluded, Qt includes added; zero findings on 6.8.3 and 6.10
- [x] C. `cs_CZ.UTF-8` generated in the Linux CI jobs
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

- Follow-up (not in this plan): on macOS and Windows the CLI end-to-end tests write logs, console
  files and the firmware cache into the runner's real user folders, because `QStandardPaths`
  ignores the `HOME` / `XDG_*` / `LOCALAPPDATA` the tests set for the child. Harmless on the
  ephemeral CI runners, not on a developer's machine. Needs a decision (e.g. a test-only data-root
  override under `HIO_TEST_OVERRIDES`).

- Windows and macOS can only be tested on CI (slow iterations, ~1 h per round).
- Follow-up on `v2/m7-packaging` after merge: plan 0015 `LSMinimumSystemVersion` 13.0 → 13.3, drop
  its temporary push trigger.
