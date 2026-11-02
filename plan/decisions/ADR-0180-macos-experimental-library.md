# ADR-0180 — Apple clang builds with `-fexperimental-library`

- **Status:** Accepted
- **Date:** 2026-10-26
- **Decided by:** Matej Strnad

## Context

The backend uses `std::jthread` / `std::stop_token` for its worker threads (CLAUDE.md: threads are owned
objects with explicit stop and join). ADR-0021 builds macOS with Apple clang from Xcode (minimum Xcode 16).
On the `macos-15` CI runner every installed Xcode (16.0–16.4, Apple clang 16.0.0 and 17.0.0) compiles
them only with `-fexperimental-library`: Apple's libc++ still gates them behind it, so the
`HioCxxFeatureChecks` check failed and every `macos-*` job stopped at configure (plan 0023). Upstream
libc++ enables them by default from LLVM 20.

## Options considered

1. **`-fexperimental-library` for Apple clang.** One flag; links Apple's static `libc++experimental.a`,
   no extra runtime library to ship or sign; Apple clang stays the macOS compiler (ADR-0021).
2. Homebrew LLVM 21 on macOS — supersedes ADR-0021 for macOS and needs its libc++ bundled or linked
   statically, which complicates signing and packaging (plan 0015).
3. A project replacement for `std::jthread` / `std::stop_token` — code change across all workers and a
   second threading idiom next to the standard one.

## Decision

Option 1. When the compiler is AppleClang, `CMakeLists.txt` appends `-fexperimental-library` to
`CMAKE_CXX_FLAGS` (global: feature checks, own and third-party targets use the same library).
Linux (libstdc++) and Windows (MSVC STL) are unchanged.

## Consequences

- macOS builds with Xcode 16 have `std::jthread` / `std::stop_token`; no new runtime dependency.
- Only `jthread` / `stop_token` may be used from the experimental part; anything else still has to be in
  all three libraries without flags (ADR-0001). Reviews watch for that.
- Drop the flag (new ADR) once the minimum Xcode's libc++ enables them by default.
