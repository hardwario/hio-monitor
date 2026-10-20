# ADR-0021 — Toolchain pins and CI hosts

- **Status:** Accepted (amended by ADR-0121: glibc baseline 2.35, packaging jobs in a Ubuntu 22.04 container)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M0 planning session)

## Context

ADR-0001 mandates clang only but left the version open. The developer machine has clang 21.1.8.
Formatting and static-analysis results must be identical locally and in CI.

## Options considered

- LLVM 19 / 20 / 21 for Linux and Windows.
- macOS: Apple clang (Xcode) vs. pinned Homebrew LLVM.
- Linux CI host: ubuntu-24.04 runner, ubuntu-22.04 + toolchain PPA, own Docker image.
- Coverage: report-only, report + gate, none.

## Decision

- **LLVM 21** (clang / clang-cl) on Linux and Windows; **clang-format 21 and clang-tidy 21** are the
  reference versions everywhere.
- **Apple clang from Xcode** on macOS (minimum Xcode 16); clang-tidy/format checks run on Linux.
- Linux CI runs on **GitHub `ubuntu-24.04`** with LLVM 21 from apt.llvm.org. The glibc baseline for
  distributable Linux packages (Ubuntu 22.04 compatibility, ADR-0012) is decided in the M7 packaging plan.
- Code coverage (llvm-cov) is **reported as a CI artifact only**, with no failing threshold for now.
- Moving to a newer LLVM major requires a new ADR (it may change formatting and tidy findings).

## Consequences

- Contributors need LLVM 21 (or must rely on CI) for format/tidy checks.
- macOS compiler versions differ from other OSes; C++23 feature checks in CMake guard against gaps.
- A coverage gate can be introduced later by a new ADR.
