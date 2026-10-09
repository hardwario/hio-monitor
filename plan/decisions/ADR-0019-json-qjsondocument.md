# ADR-0019 — JSON with Qt QJsonDocument

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M0 planning session)

## Context

open-questions #10. JSON is needed for CLI `--json` output, and later for MCP, the firmware catalog API
and nRF Util output. The backend already depends on QtCore (ADR-0004).

## Options considered

1. Qt `QJsonDocument` / `QJsonObject` — no new dependency, no exceptions, adequate performance for our sizes.
2. nlohmann/json — ergonomic, but one more dependency and it throws (needs wrapping, ADR-0007).
3. glaze — fast, reflection-based, but heavy template use and risk with MSVC STL / clang-cl.

## Decision

Use **Qt's JSON classes** everywhere. `libs/core` provides small typed helpers (conversion of
`hio::Error`, envelope builders, `std::string` ↔ UTF-8 conversions, typed field readers returning
`hio::Result`).

## Consequences

- No new dependency; parsing errors come back as values (`QJsonParseError`) and are mapped to `hio::Error`.
- The API is verbose; helpers keep call sites readable.
- Qt JSON limits (e.g. numbers as double, document size limits) must be respected; 64-bit integers that need
  exact values are serialized as strings where needed.
