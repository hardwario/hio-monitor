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

- **Never commit large or binary files directly. Use Git LFS.** Patterns are in
  `.gitattributes`; any new binary type (or any file > 100 KB that is not source text)
  must be added there *before* it is committed. Run `git lfs install` once per clone.
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
| Hardware | **SEGGER JLinkARM library only** for RTT and programming (flash/erase/reset/recover/UICR), runtime-loaded, behind interfaces; py-hardwario is the behavioural reference. Optional **nRF Util** subprocess only for nRF91 modem firmware. No nrfjprog — ADR-0015, ADR-0016, ADR-0006 |
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
- `app/cli` = headless binary (CLI + MCP over stdio) built on the same backend.
- MCP server is part of the backend (`libs/mcp`) and is exposed both over localhost HTTP in the
  GUI and over stdio in the CLI.
- New device support = new device profile, not changes scattered across the code.

## Reference material

- Old app: branch `main` of this repository (feature inventory: `plan/0002-legacy-inventory.md`).
- `../py-hardwario` — the reference implementation of flashing / RTT / PIB behaviour.
  The C++ backend reproduces its logic natively; Python is never used at runtime.

## Commands

_To be filled once the M0 skeleton exists (configure / build / test via CMake presets)._
