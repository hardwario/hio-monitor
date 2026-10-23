# ADR-0156 — `--console-file-dir` for sessions opened by `hio mcp`

- **Status:** Accepted
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (M6 implementation session, sync of plan 0013 with the code)

## Context

Every session writes a console file (M1 rules): an explicit `--console-file FILE` or a generated name in the default
directory. `hio mcp` opens several sessions, so a single file option does not fit, and plan 0013 named a
`--console-file-dir` option that does not exist.

## Options considered

1. Default directory only.
2. **New option `--console-file-dir DIR`.**

## Decision

Option 2. `hio mcp --console-file-dir DIR` makes every session it opens write its console file into `DIR`, with the
same generated file name as in the default directory. `SessionOptions` and `BleSessionOptions` gain
`std::optional<std::filesystem::path> consoleFileDir` (`ConsoleFileConfig` gets the matching field); an explicit
`consoleFile` still wins. The directory must exist and be writable, otherwise the command fails at start with
`io.*` (no session is opened). Without the option the default directory is used.

## Consequences

- Small change in `libs/device` (both session kinds), tested with the fake J-Link and the fake BLE backend.
