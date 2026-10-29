# ADR-0187 — Compiler and linker hardening in all non-sanitizer builds

- **Status:** Accepted
- **Date:** 2026-10-29
- **Decided by:** Matej Strnad

## Context
Review M-5: release binaries have no stack protector, no `_FORTIFY_SOURCE`, lazy binding and no CFI/CFG; upstream
clang enables none of these by default.

## Decision
In `HWioHardening.cmake`, for builds without sanitizers or fuzzing:
- clang: `-fstack-protector-strong`, `-fstack-clash-protection` (Linux), `-fcf-protection=full` (x86-64),
  `-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3` (Release/RelWithDebInfo), Linux link `-z relro -z now -z noexecstack`.
- clang-cl/lld-link: `/guard:cf`, `/CETCOMPAT`, `/DYNAMICBASE`, `/HIGHENTROPYVA`, `/NXCOMPAT`.
- CTest `binary_hardening_check` verifies the built executables (readelf / llvm-readobj; macOS skipped).

## Consequences
Small size/speed cost. Regressions are caught by CTest. Windows CFG interaction with the J-Link DLL verified in CI.
