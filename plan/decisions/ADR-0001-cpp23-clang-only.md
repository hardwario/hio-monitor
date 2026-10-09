# ADR-0001 — C++23 and clang-only toolchain

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (bootstrap planning session)

## Context

The app must be as memory safe as possible and live for many years. Originally C++20 was requested; C++23 brings `std::expected`, more ranges, `std::print`, deducing this. Supported platforms: Linux, Windows, macOS.

## Decision

Use **C++23**, restricted to features implemented in all three standard libraries we ship with (libstdc++ ≥ 13 on Linux, libc++ on macOS, MSVC STL on Windows). Use **clang only**: clang on Linux and macOS, **clang-cl (MSVC ABI)** on Windows. No GCC, no cl.exe.

## Consequences

- One compiler front-end → consistent warnings, sanitizers, clang-tidy everywhere.
- clang-cl is ABI compatible with MSVC-built vendor DLLs (J-Link) and official Qt msvc2022 binaries; requires MSVC Build Tools (SDK, linker) on Windows.
- A feature must be checked on all three std libs before use; CMake feature checks guard it.
