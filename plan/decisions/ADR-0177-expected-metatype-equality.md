# ADR-0177 — Types inside a meta-registered `std::expected` are equality-comparable

- **Status:** Accepted (amends ADR-0121 and plan 0015 § Linux)
- **Date:** 2026-10-26
- **Decided by:** Matej Strnad (M7 implementation start, plan 0015)

## Context

The ad-hoc 2.0.0-alpha.1 AppImage build (2026-10-26) showed that the current code does not compile with libstdc++ 13
or 14. `std::expected<hio::mcp::ToolResult, hio::Error>` is a Qt meta type. Qt's `QMetaType` instantiates
`operator==` for it, and libstdc++ before 15 declares `operator==` for `std::expected` without constraints. The
instantiation then needs `ToolResult == ToolResult`, which does not exist (`expected:1154` via `qmetatype.h:2360`).
libstdc++ 15 constrains the operator, so the host build (newer libstdc++) passed.

ADR-0121 builds Linux packages in an Ubuntu 22.04 container with `g++-13`/`libstdc++-13-dev`, and the .deb targets
Ubuntu 24.04, which ships libstdc++ from GCC 14.

## Options considered

1. Give `ToolResult`, and every other type stored in a meta-registered `std::expected`, a defaulted `operator==`
   (chosen).
2. Require libstdc++ ≥ 15. The AppImage would bundle it with checkrt, and the .deb would ship a private libstdc++
   under `/opt/hardwario-monitor`.

## Decision

- Every value or error type used inside a `std::expected` that is registered as a Qt meta type (`Q_DECLARE_METATYPE`,
  signal/slot argument, `QVariant`) has `operator==`, defaulted where its members allow it.
- The 22.04 build container keeps `g++-13`/`libstdc++-13-dev` (ADR-0121 unchanged), and the .deb keeps
  `libstdc++6 (>= 13)`.
- The container build of plan 0015 phase C1 compiles the whole tree with libstdc++ 13 and serves as the regression
  check.

## Consequences

- No private libstdc++ in the .deb. The AppImage still bundles libstdc++ with checkrt only for distributions older
  than its build baseline (as planned).
- New meta-registered `std::expected` types need an `operator==`. The container CI job catches omissions, because a
  newer host libstdc++ does not.
