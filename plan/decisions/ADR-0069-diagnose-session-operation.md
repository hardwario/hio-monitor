# ADR-0069 — Live connection checks as the session operation `Diagnose`

- **Status:** Accepted (amends feature 0002, feature 0003 and ADR-0062)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M4 planning session)

## Context

The Connect and Troubleshooting wizards show step-by-step ticks: probe found → target powered → MCU connected →
right MCU, not protected → console found. Every vendor call for a probe must run on that probe's session worker
(ADR-0031) and the probe must be locked (ADR-0058). Today a failed `open()` reports only one error.

## Options considered

1. **A new session operation `Diagnose` (chosen).**
2. A standalone checker thread with its own library copy and lock (duplicates the connect logic; conflicts with an
   open session on the same probe).
3. Call `open()` and interpret the single error (no step-by-step feedback).

## Decision

- `OperationKind` gains **`Diagnose`**. `DeviceSession::diagnose(std::string_view targetId)` →
  `Result<OperationId>`. It runs on the session worker under the probe lock; RTT is paused and restored as for
  other operations (ADR-0057, `TargetSwitch`).
- Steps (each reported as one `DiagnoseStep` result through `operationProgress`, final list in
  `OperationResult::checks`, at most 16 entries):
  1. `probe` — probe open, product, SN, firmware status (`probe.firmware_outdated` → Warning);
  2. `power` — VTref against the profile's `minTargetMillivolts` (`target.no_power`);
  3. `connect` — SWD connect to the target's J-Link device (`target.connect_failed`);
  4. `identify` — FICR part and family, protection (`target.wrong_family`, `target.protected` → Warning);
  5. `rtt` — RTT control block found within 3 s (`target.rtt_not_found`); skipped when the target is protected;
  6. `rtt-buffers` — the profile's buffer names present (`target.rtt_buffer_not_found`).
  A failed step marks the remaining steps Skipped. Diagnose never resets, halts or writes the target.
- **Probe-only sessions:** `SessionOptions` gains `bool connectTarget = true`. With `false` the worker loads the
  library, opens the probe, takes the lock and reports the new state `ProbeOpen` instead of connecting the target;
  only operations touch the target. The wizards open such a session to diagnose a target that cannot be connected
  (no power, wrong cable). `startConnection(bool attachRtt)` promotes such a session to a normal connection while
  keeping the probe lock. A tab's normal session (any state except `Stopped`) can run `Diagnose` too.
- `OperationResult` gains `std::vector<DiagnoseStep> checks` (Diagnose only).
- CLI `hio chester app|lte check` opens a probe-only session, runs `Diagnose`, prints each step.

## Consequences

- Features 0002 (session states, `connectTarget`) and 0003 (`OperationKind`, `OperationResult`) are amended and
  need re-approval; 0007 carries the amendment text.
- The same diagnosis serves the wizards, the CLI and later MCP.
