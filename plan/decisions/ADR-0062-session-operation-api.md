# ADR-0062 — Session operation API: concrete types, LTE bundle as one operation, reset as an operation

- **Status:** Accepted (amends feature 0003; complements ADR-0057)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 planning review)

## Context

Feature 0003 introduced `DeviceSession` operations (`flash`, `erase`, `recover`, `updateModem`, `readMemory`)
with `operationProgress` / `operationFinished` signals. It did not define `OperationProgress` / `OperationResult`.
The GUI (M3) needs these for its phase stepper and result card. Two more questions were open:
- An LTE bundle needs nRF Util for the modem and then J-Link for the application. Who sequences the two steps?
- M1's `Status reset(bool halt)` has no target, but ADR-0057 says every operation carries one.

## Options considered

- LTE bundle: **one session operation (chosen)** / two operations sequenced by each caller.
- Reset: **an operation with a target that replaces M1's `reset(bool)` (chosen)** / keep both paths.
- Cancel error: **reuse the M0 code `cancelled` (category Cancelled, exit 130) (chosen)** / a new
  `internal.cancelled` (proposed in the first M3 draft; dropped because it duplicates `cancelled`).

## Decision

- `OperationKind { Flash, EraseCode, EraseAll, Recover, Modem, LteBundle, Reset, ReadMemory }`.
- `OperationProgress { kind; std::span<const ProgramPhase> plannedPhases; ProgramPhase phase; int step; int steps;
  std::uint64_t done; std::uint64_t total /*0 = indeterminate*/; std::chrono::milliseconds elapsed; }`.
  `plannedPhases` is static per kind and target and includes `SwitchingTarget` / `Restoring` only when they apply.
- `OperationResult { kind; Status status; std::chrono::milliseconds duration; std::optional<ChipInfo> chip;
  std::vector<std::byte> data /*ReadMemory only, ≤ 64 KiB*/; }`. Cancelled = `status` holds the error `cancelled`.
  There is no separate flag.
- `flashLteBundle(std::shared_ptr<const LteBundle>, FlashOptions)` runs in one operation (`steps = 2`):
  1. the modem via nRF Util (probe released);
  2. the application via J-Link.

  Cancel stops between or inside steps per each step's rules. If the modem step fails, the application step does
  not run.
- `reset(std::string_view targetId, ResetMode)` → `Result<OperationId>` replaces M1's `Status reset(bool halt)`
  from M2 on. The GUI console Reset button and the CLI F4 / `--reset` pass the console target. `internal.busy`
  applies. M1 keeps its `reset(bool)` until M2 lands; M2 removes it.

## Consequences

- One sequencing implementation for the CLI `chester lte flash` and the GUI Flash page.
- The GUI stepper and the CLI progress bars share `plannedPhases` and `step/steps`.
- Feature 0003 must be updated (API, workflows, tests). Features 0004–0006 use these types as given.
