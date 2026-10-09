# 0001 — M0 Skeleton: build system, libs/core, CLI and GUI skeletons, CI

- **Status:** Draft
- **Milestone:** M0
- **Author:** Claude (with Matej Strnad), 2026-10-09
- **Related ADRs:** 0001, 0002, 0003, 0004, 0007, 0010, 0011, 0012, 0013, 0017, 0018–0026
- **Depends on:** —

## Context

v2 has no code yet. Before any hardware feature (M1+) can be built we need a project skeleton that
already enforces the long-term rules from `CLAUDE.md`: a clang-only C++23 build on three OSes, strict
warnings, sanitizers, clang-tidy/format, pinned dependencies, the `hio::Error` / `Result` error model,
logging, settings, bounded buffers, a CLI that renders errors and exit codes, an empty QML GUI, and CI
that keeps all of it green on every PR.

The old app had no tests, CI only on release, unbounded logs, a version string patched by `sed` and
silent failures (plan/0002). M0 makes those impossible to reintroduce unnoticed.

## Goals / Non-goals

**Goals**

1. `cmake --preset <os>-debug && cmake --build --preset <os>-debug && ctest --preset <os>-debug` works on
   Linux, macOS and Windows.
2. `libs/core` with `hio::Error`, `hio::Result`, error code table, JSON helpers, logging, settings,
   `RingBuffer`, `BoundedQueue`, application identity and build info — fully unit-tested.
3. `hio` CLI skeleton: global options, `--version`, `--json` envelope, error rendering, exit codes, Ctrl+C.
4. `hio-monitor` GUI skeleton: one QML window showing the version, through a ViewModel.
5. GitHub Actions CI: build + tests on 3 OSes, ASan/UBSan, TSan, clang-tidy, clang-format, qmllint,
   coverage report, LFS check.

**Non-goals**

- Any hardware access (J-Link, nRF Util, BLE), networking, MCP, firmware catalog.
- Real GUI pages, theming, icons, wizards (M3/M4).
- Packaging, signing, installers (M7). Crash reporting (open-questions #3, decided in M7).

## User experience

GUI: a single window titled **HARDWARIO Monitor** (default size 1024×700, resizable) with the centered text
"HARDWARIO Monitor 2.0.0-dev — nothing to see yet" and the build info in a smaller font. Default Qt Quick
Controls style; styling is decided in M3.

CLI: see below. Human error output on stderr:

```
Error: <message>
Cause: <cause>
What to do: <suggestion>
Details: <detail>            (line omitted when empty)
```

`Error:` is red when stderr is a TTY and `NO_COLOR` is not set.

## CLI

Executable **`hio`** (ADR-0018).

| Option / command | Behaviour |
|---|---|
| `--help`, `-h` | CLI11 help |
| `--version` | `hio 2.0.0-dev+abc1234` to stdout, exit 0. With `--json`: `{"schema":1,"ok":true,"data":{"name":"hio","version":"…","gitHash":"…","qt":"6.8.x","compiler":"…"}}` |
| `--log-level {debug,info,success,warning,error,critical}` | stderr log threshold, default `critical` (py-hardwario compatible; `success` = `info`). The file log always records debug and above. |
| `--json` | machine output, envelope per ADR-0026 |
| `_selftest error <id>` (hidden) | returns the given error code from the table → exercises rendering and exit codes. Unknown id → `usage.invalid_argument`. |
| `_selftest wait` (hidden) | blocks until cancelled (Ctrl+C / SIGTERM) → `cancelled`, exit 130 |

Parse errors from CLI11 → `usage.invalid_argument`, exit 2 (envelope with `--json`).
Exit codes per ADR-0024: 0 ok, 1 internal, 2 usage, 3 io, 4 dependency, 5 probe, 6 target, 7 firmware,
8 network, 9 timeout, 130 cancelled.

## Design

### Repository layout created by M0

```
CMakeLists.txt  CMakePresets.json  .clang-format  .clang-tidy  .editorconfig
cmake/  CPM.cmake HioDeps.cmake HioCompilerFlags.cmake HioHardening.cmake HioSanitizers.cmake
        HioCxxFeatureChecks.cmake HioVersion.cmake HioBackendRules.cmake version.h.in
libs/core/  CMakeLists.txt  include/hio/core/*.h  src/*.cpp  tests/*.cpp
app/cli/    CMakeLists.txt  src/*.{h,cpp}  tests/*.cpp
app/gui/    CMakeLists.txt  src/main.cpp  src/viewmodels/app_view_model.{h,cpp}  qml/Main.qml  tests/tst_main.qml
tools/check-lfs.sh  tools/sanitizers/tsan.supp
tests/cmake/            negative configure test for the backend Qt rule
.github/workflows/ci.yml
```

File names `snake_case.h` / `.cpp`; 4-space indent; namespaces `hio`, `hio::log`, `hio::cli`, `hio::gui`.

### Phase A — Build system

- `cmake_minimum_required(VERSION 3.28)`, `project(hio-monitor LANGUAGES CXX VERSION <from git>)`,
  `CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_EXTENSIONS OFF`, `CMAKE_EXPORT_COMPILE_COMMANDS ON`. A fatal error
  if the compiler is not Clang/AppleClang (or clang-cl on Windows), ADR-0001.
- **Presets** (Ninja, binary dir `build/<preset>`): `linux-debug`, `linux-release`, `linux-asan`
  (ASan+UBSan, `-fno-sanitize-recover=all`), `linux-tsan`, `linux-coverage`
  (`-fprofile-instr-generate -fcoverage-mapping`), `linux-tidy` (`CMAKE_CXX_CLANG_TIDY`),
  `macos-debug`, `macos-asan`, `macos-release` (`CMAKE_OSX_ARCHITECTURES=arm64;x86_64`),
  `windows-debug`, `windows-release` (clang-cl). All `macos-*` presets set `CMAKE_OSX_DEPLOYMENT_TARGET=13.0`.
  Matching build and test presets; test presets set `QT_QPA_PLATFORM=offscreen` and sanitizer options
  (`TSAN_OPTIONS=suppressions=…`). Qt is found via `CMAKE_PREFIX_PATH` from `$env{QT_ROOT_DIR}`;
  developers may override it in their git-ignored `CMakeUserPresets.json`.
- **HioCompilerFlags.cmake** → interface target `hio::warnings`: clang `-Wall -Wextra -Wpedantic -Werror
  -Wshadow -Wold-style-cast -Wnon-virtual-dtor -Wimplicit-fallthrough -Wcast-qual -Wformat=2`;
  clang-cl `/W4 /WX /permissive-` + the same `-W…` via `/clang:`. Applied to our targets only, never to
  third-party targets.
- **HioHardening.cmake** → interface target `hio::hardening`:
  Debug: `_GLIBCXX_ASSERTIONS`, `_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE`, MSVC STL debug
  iterators (default `_ITERATOR_DEBUG_LEVEL=2`).
  Release (**pending confirmation**, see decisions): `_GLIBCXX_ASSERTIONS`, `_LIBCPP_HARDENING_MODE_FAST`.
- **HioCxxFeatureChecks.cmake**: `check_cxx_source_compiles` for `__cpp_lib_expected >= 202211L`,
  `__cpp_lib_format`, `__cpp_lib_jthread`, `__cpp_lib_span`, `__cpp_lib_ranges`,
  `std::condition_variable_any` with `stop_token`. Failure → `FATAL_ERROR` naming the missing feature
  and the std lib. The allowed-features table lives in `plan/0005-build-ci-release.md`.
- **HioVersion.cmake**: `git describe --tags --match "v2.*" --dirty` → `2.0.0`, `2.0.0-rc.1`, or with
  commits after the tag `2.0.1-dev.N+<hash>`; no tag → `2.0.0-dev+<hash>`; no git → `HIO_VERSION_OVERRIDE`
  cache variable required. Generates `hio/core/version_generated.h` in the build tree (rebuilds when
  `HEAD` changes, via a configure dependency on `.git/HEAD` and the ref file).
- **HioBackendRules.cmake**: `hio_add_backend_library(<name> …)` creates a static library (ADR-0022) with
  warnings/hardening and registers it; a `cmake_language(DEFER CALL …)` at the end of the top-level
  configure walks every registered target's `LINK_LIBRARIES` / `INTERFACE_LINK_LIBRARIES` transitively
  and fails if `Qt6::Gui`, `Qt6::Quick`, `Qt6::Qml`, `Qt6::Widgets` or `Qt6::QuickControls2` appears
  (ADR-0004). A CTest test configures `tests/cmake/backend_links_gui` and expects failure.
- **HioDeps.cmake** — new dependencies (approval requested by this plan, ADR-0010):

  | Dependency | License | Use | Pin |
  |---|---|---|---|
  | CPM.cmake | MIT | dependency fetching | committed as `cmake/CPM.cmake` (text), version recorded in header comment |
  | GoogleTest + GMock | BSD-3-Clause | tests (only when `HIO_BUILD_TESTS=ON`, default ON) | release archive URL + `URL_HASH SHA256` |
  | CLI11 | BSD-3-Clause | CLI parsing (ADR-0017) | release archive URL + `URL_HASH SHA256` |

  The implementing agent uses the latest stable release of each at implementation time and records
  the exact version and SHA-256 here (table below). `CPM_SOURCE_CACHE` is supported (git-ignored
  `.cpm-cache/`).

  | Dependency | Version | SHA-256 |
  |---|---|---|
  | CPM.cmake | _tbd_ | _tbd_ |
  | GoogleTest | _tbd_ | _tbd_ |
  | CLI11 | _tbd_ | _tbd_ |

- Qt: 6.8 LTS, latest 6.8.x patch available from the official binaries; `find_package(Qt6 6.8 REQUIRED
  COMPONENTS Core)` for libs/cli, `+ Gui Qml Quick QuickControls2 QuickTest` for the GUI.
  `qt_standard_project_setup(REQUIRES 6.8)`.
- **Repo config**:
  - `.clang-format`: `BasedOnStyle: LLVM`, `IndentWidth: 4`, `ColumnLimit: 100`, `PointerAlignment: Left`,
    `AccessModifierOffset: -4`, `IncludeBlocks: Regroup` (own → Qt → std), `InsertBraces: true`.
  - `.clang-tidy`: `bugprone-*, cppcoreguidelines-*, modernize-*, performance-*, readability-*,
    misc-include-cleaner` minus documented exclusions (`modernize-use-trailing-return-type`,
    `*-magic-numbers`, `readability-identifier-length`, `cppcoreguidelines-avoid-do-while`,
    `bugprone-easily-swappable-parameters`); `WarningsAsErrors: '*'`; `HeaderFilterRegex` limited to
    `libs/` and `app/`; `readability-identifier-naming` configured per ADR-0023. Qt-parent ownership
    (`cppcoreguidelines-owning-memory`) is suppressed only at documented creation sites with `NOLINT(…)`
    plus a reason. Autogen/moc/qmltc sources get `SKIP_LINTING ON`.
  - `.editorconfig` mirroring indentation and line endings.

### Phase B — `libs/core` (target `hio_core`, alias `hio::core`; links only `Qt6::Core`)

Public headers `libs/core/include/hio/core/`:

```cpp
// error.h
namespace hio {
enum class ErrorCategory { Usage, Io, Dependency, Probe, Target, Firmware, Network, Timeout, Cancelled, Internal };
enum class ErrorCode {
    InternalUnexpected,    // "internal.unexpected"    Internal
    InternalQueueFull,     // "internal.queue_full"    Internal
    UsageInvalidArgument,  // "usage.invalid_argument" Usage
    IoFileNotFound,        // "io.file_not_found"      Io
    IoReadFailed,          // "io.read_failed"         Io
    IoWriteFailed,         // "io.write_failed"        Io
    SettingsNewerSchema,   // "settings.newer_schema"  Io
    Timeout,               // "timeout"                Timeout
    Cancelled,             // "cancelled"              Cancelled
};
enum class WizardId { FirstRunSetup, ConnectDevice, FlashFirmware, Troubleshooting, BlePairing };

struct ErrorInfo { ErrorCode code; std::string_view id; ErrorCategory category; };
inline constexpr std::array errorTable = { ErrorInfo{…}, … };   // single source of truth
[[nodiscard]] constexpr std::string_view idOf(ErrorCode) noexcept;
[[nodiscard]] constexpr ErrorCategory categoryOf(ErrorCode) noexcept;
[[nodiscard]] std::optional<ErrorCode> errorCodeFromId(std::string_view) noexcept;
[[nodiscard]] std::string_view categoryId(ErrorCategory) noexcept;   // "probe", …

struct Error {
    ErrorCode code;
    std::string message;     // what happened
    std::string cause;       // why it probably happened
    std::string suggestion;  // what the user should do
    std::string detail;      // technical detail (codes, paths, versions)
    std::optional<WizardId> wizard;
};
}
// result.h
template <class T> using Result = std::expected<T, Error>;
using Status = Result<void>;
[[nodiscard]] std::unexpected<Error> fail(ErrorCode, std::string message, std::string cause = {},
                                          std::string suggestion = {}, std::string detail = {});
```

`static_assert`s over `errorTable`: ids unique and non-empty, the table index equals the enum value, and every
enumerator is present. The M1+ plans add codes (`probe.not_found`, …) to this table; `_selftest error`
uses whatever is in the table.

- `json.h`: `[[nodiscard]] QJsonObject toJson(const Error&)` (fields per ADR-0026, empty strings kept,
  `wizard` null or kebab-case id); `QJsonObject okEnvelope(QJsonValue data)`,
  `QJsonObject errorEnvelope(const Error&)`; `std::string toCompactUtf8(const QJsonObject&)`;
  `QString toQString(std::string_view)` / `std::string toStdString(const QString&)` UTF-8 helpers.
- `app_identity.h`: `void initApplicationIdentity()` sets org `HARDWARIO`, domain `hardwario.com`,
  app `Monitor`, version (ADR-0018); called by both executables before anything else.
- `version.h`: `struct BuildInfo { std::string_view version, gitHash, compiler, qtRuntime, qtCompiled; };
  [[nodiscard]] BuildInfo buildInfo();`.
- `log.h` (ADR-0020):
  ```cpp
  namespace hio::log {
  Q_DECLARE_LOGGING_CATEGORY(lcCore)               // "hio.core"
  enum class AppKind { Gui, Cli };
  enum class Level { Debug, Info, Warning, Error, Critical };
  struct Options { Level stderrLevel = Level::Critical; std::optional<std::filesystem::path> logDir; };
  class Logging {                                  // RAII: installs handler, restores previous in dtor
  public:
      [[nodiscard]] static Result<std::unique_ptr<Logging>> init(AppKind, Options);
      ~Logging();                                  // flushes, uninstalls
      [[nodiscard]] std::filesystem::path currentFile() const;
  };
  }
  ```
  Default log dir: `QStandardPaths::AppLocalDataLocation/logs`. **Bounds:** one file per process run
  `<gui|cli>-YYYYMMDD-HHMMSS-<pid>.log`; at 10 MiB continue in `….part2.log`, …; at startup prune to the
  newest 20 files and ≤ 100 MiB total (deletion failures ignored, e.g. a file open by a running instance);
  a single message is truncated at 64 KiB. Format: `2026-10-09T12:34:56.789Z [inf] hio.core (0x7f..) text`.
  Thread-safe (one mutex in the sink), file flushed on warning and above. Fatal paths (`qFatal`,
  `std::set_terminate` handler) write a final line and flush. If the log dir is not writable, init still
  succeeds with stderr only and records a warning (the app must start without a log file).
- `settings.h` (ADR-0025):
  ```cpp
  template <class T> struct SettingKey { std::string_view path; T defaultValue; };
  class Settings {
  public:
      [[nodiscard]] static Result<Settings> openDefault();              // INI, user scope, HARDWARIO/Monitor
      [[nodiscard]] static Result<Settings> openFile(std::filesystem::path);  // tests
      template <class T> [[nodiscard]] T get(const SettingKey<T>&) const;      // default on missing/invalid type
      template <class T> [[nodiscard]] Status set(const SettingKey<T>&, const T&);  // error if read-only
      [[nodiscard]] Status sync();
      [[nodiscard]] bool isReadOnly() const noexcept;
      [[nodiscard]] int schemaVersion() const noexcept;
  };
  inline constexpr int currentSettingsSchema = 1;
  ```
  Supported `T` in M0: `bool`, `int`, `std::string`, `std::filesystem::path`. On open: missing
  `meta/schemaVersion` → write 1; lower → run the ordered migrations (M0: none, but the mechanism is tested
  with a test-only migration list); higher → read-only plus a `settings.newer_schema` warning in the log.
  Owned by one thread (documented); the GUI and CLI processes may open the same file (QSettings file locking).
- `ring_buffer.h`: `template <class T> class RingBuffer` — `explicit RingBuffer(std::size_t capacity)`
  (capacity ≥ 1, contract-checked), `push(T)` returns `bool overwrote`, `size()`, `capacity()`,
  `operator[]` (0 = oldest, bounds-checked in hardened builds), `clear()`, `droppedCount()`. Backed by
  `std::vector<T>` allocated once. Not thread-safe; the owner is documented by the user of the type.
- `bounded_queue.h`: `template <class T> class BoundedQueue` — `BoundedQueue(std::size_t capacity,
  OverflowPolicy)`, `enum class OverflowPolicy { Block, DropOldest, Reject }`;
  `[[nodiscard]] Status push(T, std::stop_token = {})` (Block waits until there is space, stop or close
  → `cancelled`; Reject on full → `internal.queue_full`; DropOldest always succeeds and counts the drop);
  `[[nodiscard]] std::optional<T> pop(std::stop_token, std::chrono::milliseconds timeout)`;
  `close()` wakes all waiters; `droppedCount()`. Mutex + `std::condition_variable_any`.

**Threading:** no threads in libs/core except those created by callers; `Logging` sink is the only
shared mutable state (owned by the `Logging` instance, guarded by its mutex).

### Phase C — CLI (`app/cli`, executable `hio`)

- `src/main.cpp`: `QCoreApplication app(argc, argv)` → `initApplicationIdentity()` → parse args →
  `Logging::init(Cli, {level})` → run → render → `return exitCode`. The whole body is wrapped in
  `try { … } catch (...)` at the top boundary only → `internal.unexpected`, exit 1 (ADR-0007).
- `src/cli_app.{h,cpp}`: `struct Context { OutputMode mode; std::stop_token stop; … };`
  `class CliApp` builds the CLI11 tree; `[[nodiscard]] Result<QJsonValue> run(…)` — commands return
  data or an error, and never print themselves (except streaming commands, via `Output`). Later milestones
  add `void registerChesterCommands(CLI::App&, Context&)` etc. in separate files.
- `src/output.{h,cpp}`: `class Output { Human | Json }`: `printResult(const QJsonValue&, humanText)`,
  `printError(const Error&)`, `printEvent(…)` (NDJSON, for M1). TTY/color detection through a small
  platform function (`isatty` / `_isatty`), respecting `NO_COLOR`. Writes through
  `std::cout` / `std::cerr` with `std::format`; no `printf` family.
- `src/exit_codes.{h,cpp}`: `[[nodiscard]] int exitCodeFor(ErrorCategory) noexcept` (ADR-0024 table).
- `src/signals.{h,cpp}`: `class SignalGuard` (RAII) — POSIX `sigaction` for SIGINT/SIGTERM setting a
  `std::atomic<bool>` (lock-free, asserted) + an owned `std::jthread` watcher (50 ms poll) calling
  `std::stop_source::request_stop()`; Windows `SetConsoleCtrlHandler` calls `request_stop()` directly.
  Destructor restores handlers and joins. A second Ctrl+C restores the default handler (forced exit).
- `src/selftest.cpp`: hidden `_selftest` commands (group name empty → not shown in help).

### Phase D — GUI (`app/gui`, executable `hio-monitor`)

- `src/main.cpp`: `QGuiApplication` → `initApplicationIdentity()` → `Logging::init(Gui, {})` →
  `Settings::openDefault()` (an error is logged, not fatal) → `QQmlApplicationEngine` loads module
  `HardwarioMonitor`, `Main`. `QObject::connect(objectCreationFailed → exit(1))`.
- `qt_add_qml_module(hio-monitor URI HardwarioMonitor VERSION 1.0 QML_FILES qml/Main.qml SOURCES
  src/viewmodels/app_view_model.{h,cpp})`.
- `AppViewModel` (`QML_ELEMENT QML_SINGLETON`): `Q_PROPERTY(QString version CONSTANT)`,
  `Q_PROPERTY(QString buildInfo CONSTANT)` — reads `hio::buildInfo()` only. No logic in QML (ADR-0003).
- Windows: `WIN32_EXECUTABLE`, macOS: `MACOSX_BUNDLE` with bundle id `com.hardwario.monitor` (icons
  later, M7).

### Phase E — CI (`.github/workflows/ci.yml`)

- Triggers: `pull_request`, `push` to `v2-dev`; `concurrency` cancels superseded runs; `permissions:
  contents: read`. **Every third-party action pinned by full commit SHA** (with version comment).
- Jobs:

  | Job | Runner | What |
  |---|---|---|
  | `lint` | ubuntu-24.04 | clang-format-21 `--dry-run --Werror` on all `*.h/*.cpp`; `tools/check-lfs.sh` (every tracked file that is binary or > 100 KB must be an LFS pointer; fails listing offenders) |
  | `linux` | ubuntu-24.04 | matrix `debug, release, asan, tsan, coverage, tidy`; LLVM 21 from apt.llvm.org; Qt via `install-qt-action` (pinned aqtinstall), cached; build; `ctest --preset … --output-on-failure`; `all_qmllint` in debug; coverage uploads `llvm-cov` HTML + lcov as artifact |
  | `macos` | macos-15 | matrix `debug, asan, release` (universal); Xcode 16+ |
  | `windows` | windows-2022 | matrix `debug, release`; pinned LLVM 21 (official installer, checksum verified) + MSVC dev environment |

- CPM source cache via `actions/cache` keyed on `hashFiles('cmake/HioDeps.cmake')`.
- Branch protection on `v2-dev` (require CI) is set up by the user on GitHub; it is not done by an agent.

### Error cases introduced in M0

| Code | Category | Message / cause / suggestion |
|---|---|---|
| `internal.unexpected` | Internal | "An unexpected internal error occurred." / "This is a bug in HARDWARIO Monitor." / "Please report it with the log file: <path>." |
| `internal.queue_full` | Internal | "An internal buffer is full." / bug or overload / report with log |
| `usage.invalid_argument` | Usage | CLI11 message / — / "Run `hio --help`." |
| `io.file_not_found`, `io.read_failed`, `io.write_failed` | Io | file path in detail; suggestion to check path/permissions |
| `settings.newer_schema` | Io | "Settings were written by a newer version and are opened read-only." / downgrade / "Changes will not be saved; update HARDWARIO Monitor." |
| `timeout` | Timeout | generic, specialized by later milestones |
| `cancelled` | Cancelled | "Cancelled by user." |

## Decisions requiring user confirmation

| Question | Options | Decision (by user) | ADR |
|---|---|---|---|
| CLI executable name | `hio` / `hardwario` / `hio-monitor-cli` / `hardwario-monitor` | `hio` | ADR-0018 |
| GUI name, exe, app id | HARDWARIO Monitor / `hio-monitor` / `com.hardwario.monitor` | as listed | ADR-0018 |
| JSON library | QJsonDocument / nlohmann / glaze | QJsonDocument | ADR-0019 |
| Logging | QLoggingCategory + own sink / spdlog / own | QLoggingCategory + own sink | ADR-0020 |
| Clang pin | LLVM 21 / 20 / 19 | LLVM 21 | ADR-0021 |
| macOS compiler | Apple clang / Homebrew LLVM | Apple clang (Xcode) | ADR-0021 |
| Linux CI host | ubuntu-24.04 / 22.04 + PPA / Docker | ubuntu-24.04 | ADR-0021 |
| Coverage | report only / gate / none | report only | ADR-0021 |
| Internal lib linkage | static / shared | static | ADR-0022 |
| Naming / style | Qt camelCase / snake_case / Google | Qt camelCase, LLVM-based format, 100 col | ADR-0023 |
| Error codes | single enum + categories / per-module domains | single enum + categories, exit code = category | ADR-0024 |
| Settings format | INI everywhere / native | INI everywhere | ADR-0025 |
| `--json` shape | versioned envelope / bare data | versioned envelope | ADR-0026 |
| Crash reporting in M0 | none / local dumps / never | none in M0, decide in M7 | open-questions #3 |
| Plan split | one plan / three plans | one phased plan | — |
| New deps CPM.cmake, GoogleTest, CLI11 | approve / reject | _pending (approval of this plan)_ | ADR-0010 |
| Hardened std lib also in **Release** (`_GLIBCXX_ASSERTIONS`, libc++ FAST mode) | yes / Debug only | _pending_ | — |
| Minor defaults: 4-space indent, snake_case `.h/.cpp` files, CMake ≥ 3.28, log limits (10 MiB/file, 20 files, 100 MiB), `--log-level` default `critical`, hidden `_selftest` | accept / change | _pending_ | — |

## Implementation steps

Phase A — build system
- [ ] A1 `.clang-format`, `.clang-tidy`, `.editorconfig`; `.gitignore` check (build/, `.cpm-cache/`)
- [ ] A2 Top-level `CMakeLists.txt`, compiler guard, `cmake/HioCompilerFlags.cmake`, `HioHardening.cmake`
- [ ] A3 `HioCxxFeatureChecks.cmake`; feature table in `plan/0005-build-ci-release.md`
- [ ] A4 `HioVersion.cmake` + `version.h.in`
- [ ] A5 `CPM.cmake` + `HioDeps.cmake` (record versions + hashes in this plan)
- [ ] A6 `HioSanitizers.cmake`, `tools/sanitizers/tsan.supp`
- [ ] A7 `HioBackendRules.cmake` + negative configure test `tests/cmake/backend_links_gui`
- [ ] A8 `CMakePresets.json` (configure/build/test presets for all three OSes)

Phase B — libs/core
- [ ] B1 `error.h/.cpp`, `result.h` + tests
- [ ] B2 `json.h/.cpp` + golden tests
- [ ] B3 `app_identity`, `version` + tests
- [ ] B4 `ring_buffer.h` + tests
- [ ] B5 `bounded_queue.h` + tests (incl. TSan stress, stop_token, close)
- [ ] B6 `log.h/.cpp` + tests (rotation, retention, truncation, concurrency, unwritable dir)
- [ ] B7 `settings.h/.cpp` + tests (defaults, types, migrations, newer schema read-only)

Phase C — CLI
- [ ] C1 `main.cpp`, `cli_app`, `output`, `exit_codes`
- [ ] C2 `signals` (SignalGuard) + `_selftest`
- [ ] C3 unit tests + end-to-end QProcess tests

Phase D — GUI
- [ ] D1 `main.cpp`, `AppViewModel`, `Main.qml`, qml module
- [ ] D2 Qt Quick Test `tst_main.qml`, `all_qmllint` clean

Phase E — CI and docs
- [ ] E1 `tools/check-lfs.sh`
- [ ] E2 `.github/workflows/ci.yml` (all jobs green)
- [ ] E3 Fill in the CLAUDE.md "Commands" section; update `plan/roadmap.md` M0 → Done

## Tests and verification

- **Unit (GTest):** everything listed in phases B and C; `ctest` runs them all, plus the Qt Quick Test and the
  negative CMake configure test.
- **Sanitizers:** `linux-asan`, `linux-tsan`, `macos-asan` presets: full test suite clean (TSan with Qt
  suppressions only; each suppression has a comment).
- **Static analysis:** `linux-tidy` builds with zero findings; clang-format dry-run clean; qmllint clean.
- **CI:** all jobs in phase E green on a PR to `v2-dev`.
- **Manual (Linux, local clang 21.1.8 / CMake 4.2, Qt 6.8.x):**
  - `hio --version` prints version; `hio --json --version | jq .data.version`.
  - `hio _selftest error io.file_not_found; echo $?` → human error block, exit 3.
  - `hio --json _selftest error cancelled; echo $?` → envelope, exit 130.
  - `hio _selftest wait`, press Ctrl+C → "Cancelled by user.", exit 130, no hang.
  - `hio --bogus; echo $?` → usage error, exit 2.
  - `hio-monitor` opens the window with the version; a log file appears under the app data `logs/` dir.
- **Manual (macOS, Windows):** the same CLI checks and the GUI window, done once by the user before Done.

## Risks and open points

- **Apple libc++ C++23 gaps** (`std::jthread`, `stop_token`, float `std::to_chars` vs. the macOS 13
  deployment target). Mitigation: feature checks fail early; minimum Xcode 16; if a gap blocks, stop and
  ask (possible deployment target change → ADR).
- **TSan with uninstrumented Qt** binaries produces false positives → suppression file, kept minimal.
- **Open-source Qt 6.8 patch releases** may end before the next LTS → tracked; an upgrade needs an ADR.
- **clang-tidy on Qt-generated code** → `SKIP_LINTING` / header filter; if noisy, document exclusions.
- **clang-cl + Qt debug binaries** (`win64_msvc2022_64` debug libs and iterator debug level must match) →
  verify in the first CI run.
- The packaging baseline for older glibc (Ubuntu 22.04) is deliberately deferred to M7 (ADR-0021).
