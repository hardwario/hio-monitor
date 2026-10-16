# ADR-0065 — M4 is split into a framework plan and three wizard plans

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M4 planning session)

## Context

M4 covers four wizards (first-run setup, connect device, troubleshooting, flash firmware) and the diagnostic report.
Several agents implement plans in parallel (ADR-0059). The wizards share the same dialog, step machine, check
results and backend checks.

## Options considered

1. **A framework plan plus three wizard plans (chosen).**
2. One plan per roadmap item (five plans, with the framework inside the first).
3. Two plans.
4. One plan for all of M4.

## Decision

| Plan | Content | Depends on |
|---|---|---|
| **0007 — wizard framework and diagnostics** | `libs/diag` (checks, process scan, USB access, report), `Diagnose` session operation, CLI `doctor` / `diag report` / `chester app\|lte check`, `WizardViewModel`, `WizardDialog` and step components, wizard launcher ("Help me fix this"), Help menu, report dialog | M3 0004; M1, M2 |
| **0008 — first-run setup wizard** | setup flow, start-up trigger | 0007, M3 0006 |
| **0009 — connect device and troubleshooting wizards** | connect flow, troubleshooting topics for every error code with a ConnectDevice / Troubleshooting wizard | 0007 |
| **0010 — flash firmware wizard** | guided flash flow, FlashFirmware topics | 0007, M3 0006 |

- 0007 defines the **wizard contract** that 0008–0010 implement: `WizardDescriptor` registration, the step
  machine API, the step component kinds and the check-result model.
- After 0007 is done, 0008, 0009 and 0010 can be implemented in parallel. Shared QML components and `Theme` still
  change only through 0004 or an amendment to it (ADR-0059); 0007 is that amendment for the wizard components.

## Consequences

- Each wizard plan is small and can be reviewed and implemented on its own.
- The wizard contract in 0007 must be stable before 0008–0010 start. Changing it needs an amendment to 0007.
