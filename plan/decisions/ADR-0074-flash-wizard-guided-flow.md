# ADR-0074 — The Flash wizard is a guided end-to-end flow

- **Status:** Accepted (complements ADR-0049)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M4 planning session)

## Context

The M3 Flash page (ADR-0049, ADR-0055) already orders the work (kind → file → options → progress). First-time
users still fail before that: no probe chosen, wrong connector, no power. FlashFirmware errors
(`firmware.hex_parse_error`, `firmware.invalid_image`, `firmware.out_of_range`, `firmware.not_found_in_catalog`)
need a target for "Help me fix this".

## Options considered

1. **A guided end-to-end flow (chosen).**
2. Error help only (a short dialog explaining file kinds).
3. Guided flow plus a catalog browser (needs open question 15).

## Decision

- Steps: device and probe (M3 picker) → what to flash (kind) → cabling for the kind's MCU + live `Diagnose` →
  firmware (file / catalog ID / recent, reusing `FirmwareLoader` and `RecentSourcesModel` from 0006) → summary and
  confirmation → progress (PhaseStepper) → result with [Open console] / [Flash another].
- Steps already satisfied are skipped (a connected tab skips device and cabling when `Diagnose` passes).
- Opened from: the Flash page ("Guide me"), the Choose-device page, Help, and "Help me fix this" on FlashFirmware
  errors (then it starts at the firmware step with the error explained).
- The Flash page stays the everyday tool; the wizard uses the same session operations.
- No catalog browsing (open question 15 stays deferred).

## Consequences

- `FirmwareLoader` and `RecentSourcesModel` (0006) are used by two view models; 0006 keeps them free of page
  state.
