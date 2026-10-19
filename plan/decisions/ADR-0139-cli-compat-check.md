# ADR-0139 — CLI compatibility with py-hardwario is checked against a generated command-tree snapshot

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M8 planning session)

## Context

CLI principle 1 (0006-cli.md): existing `hardwario` scripts keep working with only the executable renamed. There is no
systematic check yet.

## Options considered

1. **Generated table + CTest against a checked-in snapshot (chosen).**
2. Manual table in the plan.

## Decision

- `tools/cli-compat/extract_py_hardwario.py` (developer tool, run by hand, never at runtime or in normal CI) walks the
  click tree of py-hardwario **v1.7.2** (`../py-hardwario`) and writes `tests/cli-compat/py-hardwario-1.7.2.json`
  (commands, options with short/long names, flags vs values, arguments). The JSON is committed.
- `tests/cli-compat/expectations.json` marks each command/option as `same`, `renamed` (with the v2 spelling),
  `different` (with the reason) or `not-in-v2.0` (with the plan/open question). Intentional differences known now:
  `chester app pib write`, `uicr write`, `fw upload|list|info|delete`, `lte trace`, `device <family> …` (later, 0006);
  `--nrfjprog-log` (no nrfjprog, ADR-0015); console `--mcp/--mcp-listen/--mcp-token` (replaced by `hio mcp`,
  ADR-0106); console `--coredump-file`, `--device` (to be checked in 0017).
- A CTest (`cli_compat`) runs `hio` with a hidden `--dump-command-tree` option (JSON of the CLI11 tree), compares it
  with the snapshot and expectations, and fails on any unlisted difference. `docs/cli-migration.md` is generated from
  the same data.

## Consequences

- Regressions in option names are caught by CI; the snapshot is refreshed deliberately when py-hardwario changes.
- Python is only a developer tool for producing the snapshot (no runtime use, CLAUDE.md).
