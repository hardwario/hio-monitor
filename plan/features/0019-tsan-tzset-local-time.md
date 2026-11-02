# 0019 — TSan: local-time conversion (`tzset`) reported as a data race

- **Status:** In progress
- **Milestone:** M5 (maintenance; found while verifying 0011/0012)
- **Author:** Matej Strnad (with Claude), 2026-10-20
- **Related ADRs:** ADR-0001 (std libs), plan [0001](0001-m0-skeleton.md) (TSan suppressions policy)
- **Depends on:** none

## Context

`ctest --preset linux-tsan -j8` occasionally fails `tst_gui.tst_flash` / `tst_gui.tst_settings` with a
ThreadSanitizer data race inside glibc `tzset_internal`, between:

- `libs/device/src/console_file_sink.cpp` `timestamp()` (J-Link session worker thread), and
- `app/gui/src/viewmodels/console_line_model.cpp` `timeText()` → `data(TimeRole)` (GUI thread).

Both format a `std::chrono::system_clock::time_point` as local time through
`QDateTime::fromMSecsSinceEpoch(ms).toString(...)`.

### Analysis

- Qt 6 does every libc local-time call (`tzset`, `localtime_r`, `mktime`) through `qTzSet()` /
  `qLocalTime()` / `qMkTime()` in `qtenvironmentvariables.cpp`, **each under Qt's global
  `environmentMutex`** (the same mutex `qputenv` / `qgetenv` use). glibc additionally takes its own
  internal `tzset_lock` inside `tzset()` / `localtime_r()`.
- Both locks live in uninstrumented binaries (`libQt6Core`, `libc`) and are futex-based, so TSan
  never sees the happens-before edge. What it *does* see are its interceptors: `tzset_internal` calls
  `free(old_tz)` / `strdup(tz)` on every `tzset()` (Qt calls `tzset()` on every conversion), so two
  threads converting local time are reported as "write (free) vs. previous write (malloc)".
- Reproduced deterministically outside the project: a 15-line program, three `std::thread`s each
  calling `QDateTime::fromMSecsSinceEpoch(x).toString("HH:mm:ss.zzz")` in a loop, built with
  `-fsanitize=thread` against the system Qt 6.10 / glibc 2.43, reports exactly this race
  (`free` in `tzset_internal` vs. `malloc` in `strdup`) on the first run.
- Our code has no other path to the C time / environment functions (`grep` for `getenv`, `setenv`,
  `localtime`, `mktime`, `tzset`: only the single-threaded `tests/fake_nrfutil`).
- It is therefore a **TSan false positive** caused by uninstrumented locking, not a race in our
  code. In the project it is timing-dependent (needs a console line written to the file on the worker
  at the same moment as the GUI paints a time column), which is why it shows up only under `-j8` load;
  in this worktree 5× `tst_gui` at `-j8` and 2 full suites at `-j8` did not hit it.

### Why the obvious code changes do not help

- **Call `tzset()` once at startup:** Qt calls `tzset()` itself on every conversion
  (`qLocalTime()` → `tzset()` + `localtime_r()`), so glibc still frees / re-duplicates `old_tz`
  each time. No effect on the report.
- **QDateTime on both sides:** that is already the case.
- **Own mutex around our two call sites:** silences these two sites only; every other local-time
  conversion Qt does internally (QML `Date`, `QFileInfo` times, `QLocale`, log timestamps) would still
  be reported the same way. It adds a lock to the console hot path for a tool artefact.
- **`std::chrono::zoned_time` / `current_zone()`:** ADR-0001 allows only features implemented in all
  three std libs. libc++'s tzdb (`current_zone`) is still incomplete/experimental and gated by
  deployment-target availability on Apple platforms; it would also mean a second time-zone source
  next to Qt's (the GUI and the file could disagree around DST changes). Large change for a test-only
  problem.

## Goals / Non-goals

- Goal: `linux-tsan` runs of `tst_gui` are no longer flaky because of this report.
- Goal: keep TSan checking our own code (no suppression that names `libs/` or `app/` frames).
- Non-goal: change how timestamps are formatted (format, time zone, performance unchanged).

## User experience

None (test infrastructure only).

## CLI

None.

## Design

Proposed (option A): one new entry in `tools/sanitizers/tsan.supp`, written like the existing ones:

```
# Local-time conversion (QDateTime local time, QML Date, ...): Qt calls tzset()/localtime_r()/mktime()
# only under its global environmentMutex (qtenvironmentvariables.cpp: qTzSet/qLocalTime/qMkTime), and
# glibc locks tzset_lock inside them as well; both locks are futexes in uninstrumented libQt6Core / libc,
# so TSan misses the happens-before edge. What it sees is the free(old_tz) / strdup(tz) that glibc's
# tzset_internal() does on every tzset(), reported as a race between two threads formatting local time
# (seen: ConsoleFileSink timestamp() on a session worker vs. ConsoleLineModel time column on the GUI
# thread, plan/features/0019). Matches only reports with a frame inside glibc's tzset_internal.
race:tzset_internal
```

The same line (with a one-line pointer to the tsan.supp comment) is copied into the "entries of
tsan.supp" block of `tools/sanitizers/tsan-qml.supp`: the `tst_gui.*` tests set their own
`TSAN_OPTIONS` with that file (`app/gui/tests/CMakeLists.txt`), and its header requires every
`tsan.supp` entry to be repeated there (review S14). The flaky reports are in `tst_gui`, so without
this copy the fix would not reach them (found by the implementing agent: `tst_gui.tst_flash` failed
with exactly this report — `ConsoleFileSink::open` on the worker vs. `ConsoleLineModel::data` on the
GUI thread — with only `tsan.supp` changed).

No source change in `libs/` or `app/`. No threading change.

Scope of the match: a TSan `race:` suppression matches a function name in either stack of the
report. `tzset_internal` is a glibc-internal function; only reports where one access is the
`free`/`malloc` performed inside it are hidden. A real race in our code would have both accesses in
our frames and stays reported.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| How to fix the flaky TSan report | **A (recommended):** suppression `race:tzset_internal` with the reasoning above. B: own `std::mutex` around our two `QDateTime` local-time call sites (silences these two only). C: replace QDateTime local-time formatting with `std::chrono::zoned_time` (portability risk on libc++, second tz source). D: `tzset()` at startup (does not work, see Context). | **A** (user, 2026-10-20) | none (A follows the existing suppression policy of plan 0001; B/C would get ADR-0143) |

## Implementation steps

- [x] Add the commented `race:tzset_internal` entry to `tools/sanitizers/tsan.supp`.
- [x] Copy it into the "entries of tsan.supp" block of `tools/sanitizers/tsan-qml.supp`.
- [x] Add a regression test under TSan that would fail without the entry: a GTest in
      `libs/device/tests` (built in every preset) that runs `ConsoleFileSink::write()` on a
      `std::jthread` while the main thread formats local time with `QDateTime` in a loop (both a few
      thousand iterations). Without the suppression it fails deterministically under `linux-tsan`
      (`halt_on_error=1`); with it, it passes. In non-TSan presets it is a plain smoke test.
- [ ] Verify (below).

## Tests and verification

- New test fails under `linux-tsan` without the suppression line, passes with it (check both).
- `linux-debug`, `linux-asan`, `linux-tsan`: full suites green.
- `linux-tsan`: `ctest -R '^tst_gui' -j8` 5× in a row, plus the full suite at `-j8` 2×, no TSan report.
- `linux-tidy`: 0 findings; `clang-format` clean; `tools/check-includes.sh` clean.

### Results (2026-10-20)

- New test `Sink.LocalTimeOnAnotherThreadWhileTheWorkerWrites` (linux-tsan): without the line 3/3 runs
  failed with the `free` in `tzset_internal` vs. `strdup` report; with it 3/3 passed (~1 s).
- Full suites: linux-debug 1010/1010, linux-asan 1010/1010, linux-tsan 997/997, no sanitizer report.
- linux-tsan `ctest -R '^tst_gui' -j8`: runs 1-5 had 3 failures (`tst_layout`, `tst_shell`, `tst_flash`),
  each a **different** report: glib `eventfd` (wakeup fd created on a `QThread`) vs. `write` on the main
  thread, entirely inside uninstrumented `libglib-2.0`, not matched by this entry and not touched by
  this plan. Runs 6-10 in a row: all green. No `tzset_internal` report in any run.
- linux-tsan full suite `-j8` 2x: run 1 996/997 (`FakeBleCli.CommandWithManyLinesWritesThemAll`:
  301 output lines instead of 300, functional, no TSan report), run 2 997/997. Machine load average
  ~25-30 during these runs (another worktree building in parallel).
- linux-tidy: 0 clang-tidy findings (the only `warning:` is `-Wcharacter-conversion` in the CPM
  replxx sources); `clang-format` clean; `tools/check-includes.sh` clean.
- Open: the glib `eventfd` report and the FakeBleCli flake are separate issues for the user to decide.

## Risks and open points

- A future real race whose report happens to include `tzset_internal` in one stack would be hidden.
  Our code never calls the libc time / environment functions directly (all through Qt, which
  serializes them), so this cannot come from our code without bypassing Qt; review rule: no direct
  `localtime*` / `mktime` / `tzset` / `getenv` / `setenv` in `libs/` or `app/` (clang-tidy
  `concurrency-mt-unsafe` already flags most of them).
- macOS TSan is not part of the CI matrix; nothing to do there.
