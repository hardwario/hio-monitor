# ADR-0152 — libFuzzer targets in a Linux-only `linux-fuzz` preset

- **Status:** Accepted
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (M6 implementation session, sync of plan 0013 with the code)

## Context

Plan 0013 adds our own HTTP/1.1 parser and JSON-RPC handling, which are reachable by any local process. The
repository has no fuzzing support: `HioSanitizers.cmake` accepts only `address|undefined|thread`, there is no
preset and no CI job, and AppleClang ships no libFuzzer.

## Options considered

1. **New `linux-fuzz` preset** with libFuzzer targets and a short CI smoke run.
2. No fuzzing, only table-driven malformed-input tests.

## Decision

Option 1.
- `HioSanitizers.cmake` accepts `fuzzer`; preset `linux-fuzz` (clang 21, Debug, `-fsanitize=fuzzer-no-link,address,undefined`
  for libraries, `-fsanitize=fuzzer` for fuzz executables), `HIO_BUILD_FUZZERS=ON` only there.
- Targets: `http_parser_fuzz` and `json_rpc_fuzz` (in `libs/mcp/fuzz/`), each with a small seed corpus in Git.
- CI (Linux): build `linux-fuzz` and run each target for 60 s; any crash fails the job.
- macOS and Windows do not build fuzzers.

## Consequences

- One more Linux CI job (a few minutes). Table-driven parser tests stay in the normal test suite.
