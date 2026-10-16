# ADR-0070 — Exit codes and JSON of `hio doctor` and `hio chester … check`

- **Status:** Accepted (complements ADR-0024 and ADR-0026)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M4 planning session)

## Context

Check commands report several results. Scripts and CI need a meaningful exit code.

## Options considered

1. **The exit code of the first failed check's error category (chosen).**
2. Always 0 unless the command itself fails.
3. A new dedicated exit code (needs an ADR-0024 change).

## Decision

- All checks Ok, Warning or Skipped → exit 0, envelope `{"ok":true,"data":{"checks":[…]}}`.
- At least one Failed → the ADR-0024 exit code of the **first** failed check's error category (4 dependency,
  5 probe, 6 target, …). The envelope is `{"ok":false,"error":{…first failed error…},"data":{"checks":[…]}}`, so
  the full list is still available.
- Each check in JSON: `{"id","status":"ok|warning|failed|skipped","title","result","error":{…}|null}`.
- Human output: one line per check (`✓`, `!`, `✗`, `–`), the failed check's message, cause and suggestion below.

## Consequences

- ADR-0026 gains the rule that an error envelope may also carry `data` (used only by check commands).
