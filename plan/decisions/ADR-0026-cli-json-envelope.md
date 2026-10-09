# ADR-0026 — CLI `--json` output envelope

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M0 planning session)

## Context

ADR-0017 requires machine-readable `--json` output. Its shape is a public contract parsed by scripts and CI.

## Options considered

1. A versioned envelope on stdout for both success and error.
2. Bare data on stdout, an error object on stderr.

## Decision

With `--json`, every command writes **exactly one JSON object** (one line) to stdout:

```json
{"schema":1,"ok":true,"data":{ ... }}
{"schema":1,"ok":false,"error":{"code":"probe.not_found","category":"probe","message":"...","cause":"...","suggestion":"...","detail":"...","wizard":null}}
```

Streaming commands (e.g. `console`) emit **NDJSON**: one envelope per line, with event objects in `data`.
The exit code (ADR-0024) is set as well. Logs never go to stdout.

## Consequences

- One parser handles every command; `schema` allows evolving the format (a breaking change bumps it).
- Each command's `data` schema is specified in its feature plan.
