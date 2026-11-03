# ADR-0207 — LLVM lld as the linker on Linux

- **Status:** Accepted
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad (RC preparation session)

## Context

On Linux clang called the system default linker, GNU ld (BFD) from binutils, because no preset or CMake file chose
one. ADR-0001 makes clang the only compiler; Windows already links with `lld-link`, macOS with Apple's `ld`. The
package build container (packaging/linux/Containerfile) already installs `lld-21`.

## Options considered

1. **LLVM lld 21 from the same apt.llvm.org toolchain as clang (chosen).**
2. Keep GNU ld (BFD).
3. mold (fast, but a third toolchain source and not part of LLVM).

## Decision

- Every Linux build (all `linux-*` presets, own and third-party targets) links with `-fuse-ld=lld`, set once in the
  top-level `CMakeLists.txt` with `add_link_options`. The configure step fails with an install hint when the
  linker flag does not work (`lld-21` missing).
- CI installs `lld-21` next to `clang-21` in every Linux job; the build container already has it. Developers install
  `lld-21` (CLAUDE.md).
- macOS (Apple `ld`) and Windows (`lld-link` through clang-cl) are unchanged.
- The hardening link flags (ADR-0187: `-z relro -z now -z noexecstack`) stay; the CTest `binary_hardening_check`
  verifies them with lld as before.

## Consequences

- Faster links, one toolchain (clang + lld + llvm tools) from one source on Linux.
- One more package to install on a developer machine.
- The glibc symbol ceiling of the packages (ADR-0121) does not depend on the linker; `check-packages.sh` keeps
  checking it.
