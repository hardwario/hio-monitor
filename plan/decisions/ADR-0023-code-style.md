# ADR-0023 — Code style and naming

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M0 planning session)

## Context

A consistent style is needed from the first line of code; most of our calls are to Qt APIs.

## Options considered

1. Qt-style camelCase.
2. std-style snake_case.
3. Google style.

## Decision

- **Qt-style naming:** types, enums and enumerators `PascalCase`; functions, variables and parameters
  `camelCase`; private data members `m_camelCase`; constants `camelCase` (`constexpr`); macros
  `HIO_UPPER_CASE` (avoided where possible); namespaces lowercase (`hio`, `hio::cli`).
- **clang-format** based on `LLVM`, 100 columns (details in `.clang-format`, defined in the M0 plan).
- Naming is enforced by clang-tidy `readability-identifier-naming`.

## Consequences

- Our code reads consistently next to Qt calls; std types (`std::expected`) remain snake_case by nature.
- The formatter and tidy config are part of the repo and are checked in CI.
