# ADR-0038 — Confirmation for erase-all and recover

- **Status:** Accepted (amends the py-hardwario compatibility principle of ADR-0017 for these commands; amended by ADR-0153: `mem write` and `flash-write` ask too)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M2 planning session)

## Context

UX principle 5 ("safe by default") and CLI principle 7 (`--yes` for destructive operations) conflict with
py-hardwario compatibility (ADR-0017): py-hardwario never asks, and `hardwario chester app erase --all` /
`chester lte erase` erase the PIB without warning.

## Options considered

1. Ask only for new commands (recover); keep existing commands prompt-free.
2. Ask for every erase-all and recover (chosen).
3. Never ask.

## Decision

`hio chester app erase --all`, `hio chester lte erase` (always erase-all) and `hio chester app|lte recover` ask for
confirmation naming what will be lost (flash, UICR, Product Information Block). On a TTY: `[y/N]` prompt. Without a
TTY: `--yes` is required, otherwise `usage.confirmation_required` (exit 2). `flash`, `erase` (code only) and
`reset` never ask. The GUI uses an equivalent confirmation dialog (M3).

## Consequences

- Scripts using `erase --all` or `lte erase` must add `--yes` when migrating from py-hardwario
  (documented in the CLI migration notes).
- Accidental loss of the PIB from an interactive shell is prevented.
