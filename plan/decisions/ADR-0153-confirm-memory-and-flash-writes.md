# ADR-0153 — `mem write` and `flash-write` ask for confirmation (amends ADR-0038)

- **Status:** Accepted
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (M6 implementation session, sync of plan 0013 with the code)

## Context

ADR-0038 lists the CLI commands that ask [y/N] (TTY) or need `--yes`: `erase --all`, `lte erase`, `recover`,
`jlink update-firmware`. Plain `erase` does not ask. M6 adds `hio chester app|lte mem write` (RAM/peripherals) and
`flash-write` (raw bytes into flash).

## Options considered

1. **Both ask.**
2. Only `flash-write` asks (RAM writes are volatile).
3. Neither asks.

## Decision

Option 1. `mem write` and `flash-write` use `confirmDestructive` with the ADR-0038 rules: [y/N] on a TTY in human
mode; otherwise `--yes` is required (`usage.confirmation_required`); "no" → `cancelled`.

## Consequences

- Scripts must pass `--yes`. MCP tools are not affected (they use consent, ADR-0101).
- ADR-0038 gets an "amended by ADR-0153" note.
