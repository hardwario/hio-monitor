# ADR-0049 — Flash page: single-page form with in-place progress

- **Status:** Accepted; step order superseded by ADR-0055
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 GUI planning session)

## Context

The old flash page was a "flash shell" with an indeterminate progress bar, required manual console detach and
opened the catalog only as a web page. M2 provides operations with phases, cancel, auto RTT pause/re-attach and
download by catalog ID.

## Options considered

1. **One page per device tab: source → target → Flash, progress in place (chosen).**
2. A step-by-step wizard every time (the guided Flash wizard stays M4).

## Decision

- **Source** card: drop zone + "Browse…" (`.hex`, `.zip`), **catalog ID** field (32 hex digits, validated
  live, ADR-0037), **Recent** list (last 10 sources, setting `flash/recent`). Detected type is shown
  ("HEX · 412 kB · 0x0–0x64F20", "LTE bundle", "Modem firmware 1.3.7") — parsing on a worker thread.
- **Target**: `app` | `lte` segmented control; LTE input type auto-detected (ADR-0040); options
  "Halt after flashing", "Allow UICR" (only shown when the image has UICR records, ADR-0036).
- **Flash** primary button (Ctrl+Enter). While running the form is replaced by a **phase stepper**
  (Connecting → Erasing → Programming → Verifying → Resetting, or Modem), % and bytes, elapsed time and **Cancel**.
  Result card: success ("Open console", "Flash again") or inline `ErrorPanel`.
- The console is paused and re-attached automatically by the backend; the Console rail item shows a
  "busy" badge meanwhile, and console input is disabled with an explanation.
- **Maintenance** section (collapsed): Reset, Erase (code), Erase all, Recover. Erase all / LTE erase / Recover
  use the ADR-0038 confirmation dialog naming what is lost (flash, UICR, Product Information Block); the
  confirm button repeats the action ("Erase everything").

## Consequences

- `OperationProgress` / `OperationResult` (M2) need concrete fields; feature 0004 defines them and flags
  0003 to adopt them.
