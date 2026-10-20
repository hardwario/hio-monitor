# CLAUDE.md — HARDWARIO Monitor v2

HARDWARIO Monitor v2 is a complete rewrite of the HARDWARIO Monitor desktop application
(console / log / flashing tool for HARDWARIO devices, primarily CHESTER). It replaces the
old Qt app (branch `main`) entirely. The product is expected to stay in production for many
years: **stability, user-friendliness, memory safety and maintainability beat feature speed.**

All design documentation lives in [`plan/`](plan/README.md). Read `plan/README.md`,
`plan/0001-architecture.md` and the relevant ADRs in `plan/decisions/` before changing anything.

## Mandatory workflow: plan first

1. **Every feature, refactoring or architectural change starts as a plan** in
   `plan/features/NNNN-short-name.md` (use `plan/TEMPLATE-feature.md`). No code is written
   for work that has no plan in status **Approved**.
2. Plans are written and reviewed with the user. The plan author **asks the user about every
   architectural or otherwise important decision** (public interfaces, new dependencies,
   threading, data formats, UX flows, anything hard to change later). Never decide such
   things silently.
3. Each confirmed decision is recorded as an ADR in `plan/decisions/ADR-NNNN-*.md`
   (use `plan/TEMPLATE-adr.md`). ADRs are never deleted; a changed decision gets a new ADR
   that supersedes the old one.
4. **Implementation is done by a separate agent** that follows the approved plan exactly.
   If the implementation needs to deviate, stop, update the plan, and ask the user.
5. Plan status lifecycle: `Draft → Approved → In progress → Done` (or `Rejected` / `Superseded`).
   The implementing agent updates the status and ticks the plan's checklist.
6. A feature is done only when its verification section passes (tests, sanitizers, clang-tidy,
   manual checks described in the plan).

## Repository rules

- **Never commit binary files directly. Use Git LFS.** Patterns are in `.gitattributes`; any new binary type
  must be added there *before* it is committed, and so must generated text over 100 KB (firmware `.hex`, linker
  maps). Other text files (sources, plans, `.ts`, docs) stay in plain Git at any size (ADR-0112).
  Run `git lfs install` once per clone.
- **Never commit vendor SDK binaries** (SEGGER J-Link, Nordic nRF Util / nrfjprog, Qt) — not even via LFS.
  They are loaded at runtime from the user's installation (ADR-0006).
- Don't commit secrets, tokens, signing certificates or `.env` files.
- Commit only when asked; never push or rewrite shared history without the user's approval.

## Technology constraints (see ADRs)

| Area | Rule |
|---|---|
| Language | **C++23**, only features implemented in all three std libs we ship with: libstdc++ (Linux), libc++ (macOS), MSVC STL (Windows) — ADR-0001 |
| Compiler | **clang only**: clang on Linux/macOS, **clang-cl** (MSVC ABI) on Windows. No GCC, no cl.exe — ADR-0001 |
| Framework | **Qt 6.8 LTS**, Community edition (LGPLv3): dynamic linking only, no commercial-only modules — ADR-0002 |
| UI | **Qt Quick / QML**, thin; no business logic in QML — ADR-0003 |
| Backend | May use QtCore, QtNetwork, QtBluetooth; **never QtGui / QtQuick / QtWidgets** — ADR-0004 |
| Hardware | **SEGGER JLinkARM library only** (≥ 8.00) for RTT and programming (flash/erase/reset/recover/UICR), runtime-loaded from own declarations, one library copy per session, behind interfaces; py-hardwario / rttt 1.9.2 are the behavioural reference. Optional **nRF Util** subprocess only for nRF91 modem firmware. No nrfjprog — ADR-0015, ADR-0016, ADR-0006, ADR-0028–0030, ADR-0034 |
| Localization | **English + Czech** via Qt Linguist (English source text), extensible; every user-visible string translatable; backend translates at creation; CLI English only — ADR-0027 |
| Errors | `std::expected<T, hio::Error>`; no exceptions crossing module, thread or C-callback boundaries — ADR-0007 |
| Build | CMake + `CMakePresets.json`; third-party deps via **CPM / FetchContent pinned to exact tags + hashes** (no Conan, no vcpkg) — ADR-0010 |
| Tests | **GoogleTest + GMock** for C++, Qt Quick Test for QML — ADR-0010 |

## Memory-safety and code rules

- No owning raw pointers, no `new` / `delete`, no `malloc` / `free`. Use `std::unique_ptr`,
  `std::make_unique`, values, or Qt parent ownership (documented at the creation site).
- Non-owning views: `std::span`, `std::string_view`, references. No pointer + length pairs.
- No C arrays (`std::array`), no C-style casts, no `reinterpret_cast` outside the vendor
  adapter modules, no varargs, no `printf`-family functions (use `std::format` / Qt).
- All functions returning `std::expected` (or anything that must not be ignored) are `[[nodiscard]]`.
- Every buffer, queue, log and history is **bounded** and has a documented limit.
- Vendor APIs (JLinkARM library, nRF Util process) are only touched inside `libs/hw/*` adapters, wrapped
  in RAII types. C callbacks never throw and never call back into Qt objects directly.
- Each hardware session is owned by exactly one worker thread; all vendor-DLL calls for that
  probe are serialized on it. Cross-thread communication only via queued signals or
  thread-safe queues. No shared mutable state without a documented owner/lock.
- Threads are owned objects (`std::jthread` / owned `QThread`) with explicit stop and join —
  no fire-and-forget threads.
- Compiler flags: `-Wall -Wextra -Wpedantic -Werror` (clang-cl equivalents), hardened standard
  library assertions in Debug, ASan + UBSan and TSan CI jobs, `clang-tidy`
  (bugprone, cppcoreguidelines, modernize, performance, readability subset) and `clang-format` clean.
- Every user-visible error goes through `hio::Error`: what happened, why it probably happened,
  what the user should do, optional wizard to launch. No raw error codes in the UI.

## Architecture rules (summary of `plan/0001-architecture.md`)

- `libs/*` = backend, UI-independent, buildable and testable headless.
- `app/gui` = the only place using QtQuick; talks to the backend only through ViewModels.
- `app/cli` = headless binary built on the same backend: py-hardwario-compatible commands,
  `--json` output, MCP over stdio (`plan/0006-cli.md`, ADR-0017). Every backend feature also
  gets its CLI command; the CLI contains no logic of its own.
- MCP server is part of the backend (`libs/mcp`) and is exposed both over localhost HTTP in the
  GUI and over stdio in the CLI.
- New device support = new device profile, not changes scattered across the code.

## Reference material

- Old app: branch `main` of this repository (feature inventory: `plan/0002-legacy-inventory.md`).
- `../py-hardwario` — the reference implementation of flashing / RTT / PIB behaviour.
  The C++ backend reproduces its logic natively; Python is never used at runtime.

## Commands

Presets live in `CMakePresets.json` (`linux-*`, `macos-*`, `windows-*`); build trees go to `build/<preset>`,
CPM sources to `.cpm-cache/` (both git-ignored). Qt is found via `$QT_ROOT_DIR` (CI, Qt installer) or the
system Qt; a personal `CMakeUserPresets.json` may override it. Linux needs clang 21 (`clang++-21`).

```sh
cmake --preset linux-debug                      # configure (also: linux-release, -asan, -tsan, -coverage, -tidy)
cmake --build --preset linux-debug              # build everything
ctest --preset linux-debug --output-on-failure  # unit, CLI end-to-end, Qt Quick and CMake tests
cmake --build --preset linux-tidy               # clang-tidy build: must have zero findings
cmake --build --preset linux-debug --target all_qmllint           # QML lint
cmake --build --preset linux-debug --target update_translations   # after changing user-visible strings
cmake --build --preset linux-debug --target check_translations    # .ts freshness (CI), unfinished cs count
git ls-files -z -- '*.h' '*.cpp' | xargs -0 clang-format-21 --dry-run --Werror   # formatting
tools/check-lfs.sh [--untracked]                # Git LFS rule (binary, generated text > 100 KB)
tools/check-includes.sh                         # no <bits/...> includes (libc++ / MSVC STL)
build/linux-debug/app/cli/hio --help            # CLI; GUI: build/linux-debug/app/gui/hio-monitor
build/linux-debug/app/cli/hio ble scan          # nearby CHESTERs (--all: every device); then pick one by -a ID or --name N:
build/linux-debug/app/cli/hio ble console -a F1:22:33:44:55:66   # also `ble command CMD`, `ble pair` (Linux/Windows)
HIO_FAKE_BLE_SCENARIO=tests/ble_scenarios/chester.json build/linux-debug/app/cli/hio ble scan   # fake BLE (test builds)
build/linux-debug/app/cli/hio jlink list        # probes (J-Link Software 8.x: auto-discovered, or
                                                # --jlink-library PATH / $HIO_JLINK_LIBRARY)
build/linux-debug/app/cli/hio chester app console   # RTT console; `chester app command version`
# Without hardware: the test-only fake J-Link library and a JSON scenario (tests/fake_jlink/fake_jlink.cpp)
HIO_JLINK_LIBRARY=build/linux-debug/tests/fake_jlink/libhio_fake_jlink.so \
  HIO_FAKE_JLINK_SCENARIO=scenario.json build/linux-debug/app/cli/hio chester app console
HIO_HIL=1 ctest --preset linux-debug -L hil --output-on-failure   # hardware-in-the-loop (CHESTER + J-Link)
```

Translations: run `update_translations`, translate the new entries of `i18n/hio_cs.ts` (Qt Linguist), commit
both `.ts` files. CI (`.github/workflows/ci.yml`) runs all of the above on Linux plus macOS and Windows builds.
