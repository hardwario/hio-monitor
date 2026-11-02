# 0020 — TSan: glib event-dispatcher wakeup fd reported as a data race in `tst_gui`

- **Status:** Done
- **Milestone:** M5 (maintenance; found while verifying plan 0019)
- **Author:** Matej Strnad (with Claude), 2026-10-20
- **Related ADRs:** plan [0001](0001-m0-skeleton.md) (TSan suppressions policy), ADR-0141 (BLE thread event loop)
- **Depends on:** none (independent of 0019; both touch `tools/sanitizers/tsan-qml.supp`, different lines)

## Context

`ctest --preset linux-tsan -j8 -R '^tst_gui'` fails about 3 runs in 10 under load, in `tst_layout`,
`tst_shell` or `tst_flash` (seen while verifying 0019, logs `build/v0019/tsan-gui-{2,3,5}.log` in the
0019 worktree). The report, entirely inside uninstrumented `libglib-2.0`:

```
WARNING: ThreadSanitizer: data race
  Read of size 8 by main thread:
    #0 write
    #1 <null> (libglib-2.0.so.0+0xb9047)            glib/gwakeup.c:256  g_wakeup_signal()
  Previous write of size 8 by thread T4:
    #0 eventfd
    #1 <null> (libglib-2.0.so.0+0xbf54e)            glib/gwakeup.c:147  g_wakeup_new()
  Location is file descriptor 5 created by thread T4
  Thread T4 created by main thread at: pthread_create / QThread::start (libQt6Core)
```

### Which thread is T4

Traced `tst_gui -input tst_layout.qml` (linux-tsan build) under gdb, breaking on `pthread_create` and
`eventfd`. The threads are created in a fixed order, all from the main thread:

| TSan id | Thread | Created by |
|---|---|---|
| (none) | TSan background thread | `__tsan::MaybeSpawnBackgroundThread` |
| T1 | `ProbeScanner` worker (`std::jthread`) | `AppViewModel` ctor |
| T2 | `DiagWorker` worker (`std::jthread`) | `AppViewModel` ctor |
| T3 | `FirmwareLoader` worker (`std::jthread`) | `FirmwareServices` ctor |
| **T4** | **`QQmlThread`** (QML type loader) | `QQmlTypeLoader::startThread()` → `QThread::start` |
| T5, T6 | `Thread (pooled)` | Qt's global thread pool |

T4 is Qt's QML type loader thread. It is the first `QThread::start` in the process, which matches the
report's creation stack. Our three jthreads never call `eventfd` (no event loop).

### Mechanism

1. `QQmlThread` starts and creates its `QEventDispatcherGlib`. That calls `g_main_context_new()` →
   `g_wakeup_new()` → **`eventfd()`** on T4. TSan's `eventfd` interceptor records the new fd as written
   by T4.
2. Qt publishes the dispatcher to other threads through `QThreadData::eventDispatcher` (an atomic
   store-release / load-acquire). `QQmlThread::startup()` also waits for the thread with
   `QMutex` / `QWaitCondition`. All of this is inlined into **uninstrumented** `libQt6Core` / `libQt6Qml`,
   and Qt 6's `QMutex` is futex-based. TSan never sees the happens-before edge.
3. The main thread posts an event to the type loader (`QCoreApplication::postEvent` →
   `QEventDispatcherGlib::wakeUp()` → `g_main_context_wakeup()` → `g_wakeup_signal()` →
   **`write(fd)`**). TSan's `write` interceptor reads the fd's state, finds no ordering against the
   `eventfd()` on T4, and reports a race.

The main thread can only reach the fd through the dispatcher pointer that T4 published, so the
ordering does exist. This is a **TSan false positive caused by uninstrumented synchronization**,
not a race in our code or in Qt.

It is flaky because the report only fires when no TSan-visible synchronization happens to order the
two calls first. Whether one does depends on scheduling, which is why it shows up under `-j8` load.

### Reproduced outside the project

A 20-line program built with `-fsanitize=thread` against the system Qt 6.10 / glib 2.88: one `QThread`
with an event loop, the main thread posts 1000 queued `invokeMethod` calls to an object living there.
The suppressions are the project's Qt entries (`called_from_lib:libQt6Core.so.6`, `thread:QThread::start`,
`race:QObject::moveToThread`, `race:QMetaObject::invokeMethod`, plus the inlined slot-object frames).

| Variant | Runs with the glib `eventfd`/`write` report |
|---|---|
| unchanged | **10 / 10** (identical report: `gwakeup.c:147` vs. `gwakeup.c:256`) |
| `QT_NO_GLIB=1` in the environment | 0 / 10 |
| `+ called_from_lib:libglib-2.0.so.0` | 0 / 10 |

### Why our own event-loop threads are not affected

The BLE thread (ADR-0141) is a `std::jthread` running a `QEventLoop`, so it also creates a glib wakeup
fd. `BleHost` hands work to it under `std::mutex` (`m_startMutex`, `workerMutex`), which TSan sees, so
the BLE tests run clean with the strict `tsan.supp`. Only Qt-internal threads that synchronize purely
inside Qt (`QQmlThread`, `Thread (pooled)`) hit this, and those exist only in the Qt Quick tests.

## Goals / Non-goals

- Goal: `linux-tsan` `tst_gui.*` no longer flaky because of this report.
- Goal: keep TSan strict on our own code (no entry that names `libs/` or `app/` frames, `tsan.supp`
  unchanged).
- Non-goal: change the application's event dispatcher or threading.

## User experience

None (test infrastructure only).

## CLI

None.

## Design

### Option A (recommended): `called_from_lib:libglib-2.0.so.0` in `tsan-qml.supp`

Add to the Qt block at the top of `tools/sanitizers/tsan-qml.supp` (not to the mirrored
"entries of tsan.supp" block, and not to `tsan.supp`):

```
# Qt's glib event dispatcher (QEventDispatcherGlib, the default on Linux): every thread with an event
# loop gets a GMainContext whose wakeup eventfd is created on that thread (g_wakeup_new) and written by
# the threads that post events to it (g_wakeup_signal). Qt publishes the dispatcher through atomics and
# QMutex/QWaitCondition inside uninstrumented libQt6Core, so TSan misses the ordering and reports
# "eventfd on QQmlThread vs. write on the main thread" (plan/features/0020). called_from_lib ignores the
# interceptors (eventfd, read, write, poll, malloc, free) called from libglib itself.
called_from_lib:libglib-2.0.so.0
```

- Same mechanism and policy as the existing `called_from_lib:libQt6*` lines: an uninstrumented
  third-party library, a module-scoped match, commented.
- The tests keep the **same dispatcher as the shipped app** on Linux (glib).
- What it hides: TSan's view of fds and heap blocks that glib itself creates or touches. Our code never
  calls glib directly (`grep` for `g_` / `<glib.h>` in `libs/` and `app/`: none). A race between our
  code and a glib-allocated block would need our code to hold glib memory, which it doesn't.
- Only in `tsan-qml.supp`: `tsan.supp` stays strict. The S14 mirror rule (copy every `tsan.supp`
  entry into `tsan-qml.supp`) is unaffected, because this entry belongs to `tsan-qml.supp` only, like
  the `libQt6*` lines.

### Option B: `QT_NO_GLIB=1` in the `tst_gui` test environment

Append `QT_NO_GLIB=1` to the `ENVIRONMENT` of `tst_gui.*` in `app/gui/tests/CMakeLists.txt`. Qt then
uses its own `QEventDispatcherUNIX` (`QThreadPipe` wakeup eventfd in `libQt6Core`), whose interceptor
calls are already ignored by `called_from_lib:libQt6Core.so.6`.

- No new suppression line.
- But the Qt Quick tests would run a **different event dispatcher** than users get on Linux (timer
  coalescing and event ordering differ slightly between the glib and UNIX dispatchers), only in TSan
  terms "to make it quiet". It applies to every preset (debug, ASan, TSan), unless made TSan-only.
- Running `tst_gui` by hand without the ctest environment would bring the report back.

### Option C (rejected): `race:` suppression or a global TSan flag

- `race:libglib-2.0.so.0` also matches module names in any frame. It works, but it only hides the
  report, and TSan still tracks glib's fds. It is broader in intent and not better than A.
- `TSAN_OPTIONS=ignore_noninstrumented_modules=1` ignores interceptors from **every** uninstrumented
  library (libstdc++, Qt, libc helpers). That is far too broad for the strict policy.
- Building glib / Qt with TSan: out of scope for a test-only false positive.

No source change in `libs/` or `app/`. No threading change.

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| How to fix the flaky glib wakeup-fd report | **A (recommended):** `called_from_lib:libglib-2.0.so.0` in `tsan-qml.supp` only. B: `QT_NO_GLIB=1` in the `tst_gui` test environment. C: `race:` / global TSan flag (rejected above). | **A** (user, 2026-10-20) | none (A and B follow the suppression policy of plan 0001) |
| Regression test | **No new test (recommended):** the trigger is Qt-internal (`QQmlThread`); verification is the 10× `tst_gui` run below plus the scratch repro recorded above. Alternatively a C++ test in `tst_gui`'s `Setup` that posts to a `QThread` event loop (needs the `tst_gui` env to see the `tsan-qml.supp` line). | **No new test** (user, 2026-10-20) | none |

| Revised after verification: A alone uncovers the qmlcache `Registry` teardown report, and so does B (see Follow-up checks) | **A2 (recommended):** A + `race:Registry::lookupCachedUnit` in `tsan-qml.supp`. B2: `QT_NO_GLIB=1` + the same line. Revert: drop 0020 and keep the ~3/10 flake. | **A2** (user, 2026-10-20) | none |

## Implementation steps

- [x] Apply the chosen option (A: the commented line above in `tools/sanitizers/tsan-qml.supp`;
      B: `QT_NO_GLIB=1` appended to the `tst_gui.*` `ENVIRONMENT` with a comment).
- [x] Verify (below) and record results in this plan. **Verification failed** (see Results): the
      entry removes the glib report but exposes a new, more frequent one. Needs a decision.
- [x] A2: add the commented `race:Registry::lookupCachedUnit` entry (text from "Revised proposal: A2")
      below the glib entry in `tools/sanitizers/tsan-qml.supp`, above the mirrored `tsan.supp` block.
- [x] A2: verify (below) and record results in this plan (see Results A2).

## Tests and verification

- `linux-tsan`: `ctest --preset linux-tsan -j8 -R '^tst_gui'` **10× in a row**, no TSan report.
  Run the builds in the worktree's `build/` (`/tmp` is a small tmpfs).
- `linux-debug`, `linux-asan`, `linux-tsan`: full suites green.
- `linux-tidy` / `clang-format` / `tools/check-includes.sh`: unaffected (no source change), run once.
- If 0019 is not merged yet, the `tzset_internal` report may still appear in `tst_gui` runs. Count it
  separately; it is not a failure of this plan.

### Results (2026-10-20)

Option A applied (`called_from_lib:libglib-2.0.so.0` in `tsan-qml.supp`, nothing else changed). Builds
in the worktree's `build/`, `DEBUGINFOD_URLS=` (empty) for every ctest run, logs in `build/v0020/`.

**`ctest --preset linux-tsan -j8 -R '^tst_gui'`, 10× (`tsan-gui-{1..10}.log`): FAILED, 10 / 10 runs.**

| Report | Reports in 10 runs | Runs |
|---|---|---|
| glib `eventfd` / `write` (this plan) | 0 | 0 / 10 |
| `tzset_internal` (plan 0019, not on this branch) | 5 | 5 / 10 |
| **new:** `Registry::~Registry` (qmlcache loader) vs. `QQmlThread` | 49 | 10 / 10 |

Failed tests per run: 5, 7, 4, 6, 6, 5, 3, 7, 5, 6 of 14 (`tst_already_running`, `tst_main`, `tst_flash`,
`tst_wizard`, `tst_shortcuts`, `tst_layout`, ...).

The new report: at exit the main thread destroys the generated QML cache-unit registry
(`build/linux-tsan/app/gui/.rcc/qmlcache/hio_gui_qmlcache_loader.cpp`, a `Q_GLOBAL_STATIC` whose
`QHash<QString, CachedQmlUnit const *>` is freed by `cxa_at_exit`), "previous read" by T4 (`QQmlThread`,
already finished) in `Registry::lookupCachedUnit` ← `QQmlMetaType::findCachedCompilationUnit`.
T4 is stopped and joined inside uninstrumented Qt, so TSan needs some other happens-before edge.

**Baseline without the entry** (line removed temporarily, same build, same load, 5 runs,
`baseline-tsan-gui-{1..5}.log`; the file was restored afterwards): `Registry::~Registry` **0**,
glib `eventfd`/`write` 3 (2 runs), `tzset_internal` 2, one `QTestData::~QTestData` vs.
`__vsnprintf_chk` in `libQt6Test` (T1). 1 run of 5 clean.

So the entry causes the new report. Likely mechanism: TSan models fd I/O as synchronization
(`write` = release, `read` / `poll` = acquire on the fd, `close` too). The glib wakeup fd traffic
between the main thread and `QQmlThread` was the TSan-visible happens-before edge that ordered
`QQmlThread`'s work before the main thread's static destruction. `called_from_lib` ignores those
interceptors, so the edge is gone. The same would apply to other edges glib provided, so Option A
removes more than the false positive. Option B (`QT_NO_GLIB=1`) keeps fd sync, but in
`libQt6Core` (already `called_from_lib`), so it may show the same effect; not tried (needs a decision).

**Full suites (`-j8`):**

- `linux-debug`: 1009 / 1009 passed.
- `linux-asan`: 1008 / 1009; `FakeBleCli.CommandWithManyLinesWritesThemAll` failed
  (`ble_e2e_test.cpp:305`, 301 lines), passed 3 / 3 on rerun alone: load flake, unrelated (ASan
  does not read the TSan files).
- `linux-tsan`: 990 / 996; `tst_gui.{tst_already_running,tst_flash,tst_layout,tst_main,tst_wizard}`
  (4× `Registry::~Registry`, 1× `tzset_internal`), plus `Diagnose.OverallBudgetSkipsTheRemainingSteps`
  (`diagnose_session_test.cpp:484`, steps ran within the budget), passed 3 / 3 on rerun alone: timing
  flake under load, uses `tsan.supp`, unrelated.

**Static checks:** `linux-tidy` build: 0 findings in our code (one `-Wcharacter-conversion` warning in
third-party `replxx` from `.cpm-cache`). `clang-format-21 --dry-run --Werror`: clean.
`tools/check-includes.sh`: passed.

**Outcome:** not done. Option A trades a ~3-in-10 flaky report for a report in every run. Needs a new
decision (e.g. try Option B, or Option A plus a suppression for the qmlcache `Registry` teardown, or a
different approach); the status stays In progress.

### Follow-up checks (2026-10-20, planning session)

Same `linux-tsan` build, all 14 `tst_gui` files started in parallel (one process each, the `tst_gui`
environment set by hand, `DEBUGINFOD_URLS=`), logs in `build/v0020/optB/` and `build/v0020/optA2/`:

| Variant | Rounds | Failed processes | `~Registry` | glib | `tzset_internal` |
|---|---|---|---|---|---|
| B: `QT_NO_GLIB=1`, original `tsan-qml.supp` | 3 | 15 | **15** | 0 | 0 |
| A + `race:Registry::lookupCachedUnit` | 5 | 3 (all `tst_flash`) | 0 | 0 | 3 (plan 0019) |

- Option B exposes the same `Registry` report. The Qt UNIX dispatcher's wakeup fd is driven from
  `libQt6Core` (`called_from_lib`), so its fd I/O also stops counting as synchronization. Any fix that
  takes the wakeup-fd traffic out of TSan's view uncovers the `Registry` teardown. Without a fix it was
  hidden only by that incidental release/acquire on the fd.
- The `Registry` report is itself a false positive of the same kind. `QQmlThread` is stopped and joined
  (`QThread::quit()` / `wait()`) inside uninstrumented `libQt6Qml` / `libQt6Core` before
  `exit()` runs the `Q_GLOBAL_STATIC` destructors. The registry is **qmlcachegen-generated** code
  (`build/*/app/gui/.rcc/qmlcache/hio_gui_qmlcache_loader.cpp`), compiled into our binary and therefore
  instrumented, so `called_from_lib` cannot cover it.

### Revised proposal: A2 = Option A + one suppression for the generated cache registry

Add below the glib entry in `tools/sanitizers/tsan-qml.supp`:

```
# The QML cache-unit registry generated by qmlcachegen (.rcc/qmlcache/*_qmlcache_loader.cpp, compiled
# into our binary) is read by QQmlThread (lookupCachedUnit) and destroyed by exit() on the main thread.
# Qt joins QQmlThread inside uninstrumented libQt6Qml/libQt6Core first; TSan used to see an ordering only
# through the glib wakeup fd, which the entry above hides (plan/features/0020).
race:Registry::lookupCachedUnit
```

It matches only reports with the generated `lookupCachedUnit` in a stack. Our code never calls it.
Verification as above (10× `tst_gui` at `-j8`, full suites). `tzset_internal` reports are expected
until plan 0019 is merged.

### Results A2 (2026-10-20)

A2 applied: the `race:Registry::lookupCachedUnit` entry added below the glib entry in
`tools/sanitizers/tsan-qml.supp`; `tsan.supp`, `libs/` and `app/` unchanged. The tests read the
suppressions file from the source tree at runtime (`TSAN_OPTIONS=suppressions=...`), so the existing
`linux-tsan` build was used without a rebuild. `DEBUGINFOD_URLS=` (empty) for every ctest run, logs in
`build/v0020/`.

**`ctest --preset linux-tsan -j8 -R '^tst_gui'`, 10× (`a2-tsan-gui-{1..10}.log`): passed, 10 / 10 runs,
14 / 14 tests each** (`halt_on_error=1`, so any report would fail a test).

| Report | Reports in 10 runs | Runs |
|---|---|---|
| glib `eventfd` / `write` (this plan) | 0 | 0 / 10 |
| `Registry::~Registry` (qmlcache loader) | 0 | 0 / 10 |
| `tzset_internal` (plan 0019, not on this branch) | 0 | 0 / 10 |
| any other | 0 | 0 / 10 |

**Full suites (`-j8`):**

- `linux-tsan` (`a2-full-linux-tsan.log`): 996 / 996 passed, no TSan report.
- `linux-debug` / `linux-asan`: not rerun; they do not read the TSan suppression files, so the results
  above (debug 1009 / 1009, asan 1008 / 1009 with the `FakeBleCli` load flake, 3 / 3 alone) still hold.

**Static checks:** no source change, so the tidy / `clang-format` / `tools/check-includes.sh` results
above still apply.

**Outcome:** done. The glib wakeup-fd flake and the `Registry` teardown report it uncovered are both gone.

## Risks and open points

- A real race whose only evidence is a glib-allocated block or fd would be hidden in `tst_gui`. Our
  code does not use glib directly, so this cannot come from our code. Review rule: no direct glib API
  use in `libs/` or `app/`.
- Side finding: the environment sets `DEBUGINFOD_URLS=https://debuginfod.ubuntu.com`. TSan's
  `llvm-symbolizer` then queries that server for every symbolized report, including reports that are
  matched by `race:` / `thread:` suppressions. In the scratch repro this stalled runs for 30 s and more.
  Possibly part of the "only under load" slowness. Separate follow-up: unset `DEBUGINFOD_URLS` in the
  sanitizer test presets.
