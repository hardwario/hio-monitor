# ADR-0024 — Error codes, categories and CLI exit codes

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M0 planning session)

## Context

`hio::Error` (ADR-0007) needs a code. The codes surface in the CLI exit status and in the `--json`
`code` field, both of which are public contracts (ADR-0017).

## Options considered

1. A single `enum class ErrorCode` with stable string ids, each mapped to an `ErrorCategory`.
2. Per-module error domains (like `std::error_category`).

## Decision

- One **`enum class hio::ErrorCode`** for the whole backend, defined together with a single `constexpr`
  table `{code, stable id, category}`. Ids are dotted lowercase strings (`probe.not_found`) and never change
  meaning once released.
- **`enum class hio::ErrorCategory`**: `Usage, Io, Dependency, Probe, Target, Firmware, Network,
  Timeout, Cancelled, Internal`.
- `hio::Error` = `{ code, message (what happened), cause (likely why), suggestion (what to do),
  detail (technical), optional wizard }`.
- **CLI exit code = category**: 0 ok, 1 internal, 2 usage, 3 io, 4 dependency, 5 probe, 6 target,
  7 firmware, 8 network, 9 timeout, 130 cancelled.

## Consequences

- One searchable list of all errors; adding a code means one table row (+ tests check the table).
- Scripts can branch on the exit code (coarse) or on the JSON `code` id (fine-grained).
- New categories change the exit-code contract and need a new ADR.
