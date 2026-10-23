# ADR-0174 — Flash page: two kinds, one LTE card (LTE firmware and modem merged)

- **Status:** Accepted (supersedes the kind list of ADR-0055)
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (GUI feedback round 1, plan 0021)

## Context

ADR-0055 gives the Flash page three kind cards: Application firmware, LTE firmware and LTE modem firmware. In use,
the split between "LTE firmware" and "LTE modem firmware" is confusing: CHESTER has one designated LTE firmware
(ADR-0165), published as an LTE bundle that contains both the LTE application and the modem firmware. Users should
not have to know which part they need.

## Options considered

1. Keep three cards (ADR-0055).
2. Two cards: Application firmware and LTE firmware; the LTE card covers the LTE MCU application, the modem and the
   bundle (chosen).

## Decision

- Step 1 "What do you want to flash?" has **two** cards: **Application firmware** (target `app`) and **LTE firmware**
  (target `lte`).
- The LTE card flashes the designated LTE firmware by default (ADR-0165). "Use your own file…" accepts every LTE
  input: an LTE `.hex`, an LTE bundle `.zip` or a modem `mfw_nrf91*.zip`. The input is classified per ADR-0040 and
  the classification picks the operation (`flash`, `flashLteBundle`, `updateModem`). No catalog ID on the LTE card.
- The kind-mismatch hint stays only between the two cards (e.g. an LTE bundle dropped on Application firmware:
  "This is LTE firmware — switch to LTE firmware?" [Switch]); it never switches silently.
- The nRF Util prerequisite is shown on the LTE card whenever the chosen LTE source contains modem firmware (the
  designated bundle always does).
- Everything else in ADR-0055 (default kind from the tab's target, sticky Flash bar, options, prerequisites inline)
  and ADR-0049 (progress, result, maintenance) stays.

## Consequences

- One card less; the modem is no longer a separate concept in the GUI.
- `FlashViewModel::Kind` loses `ModemFirmware`; the operation is derived from the loaded LTE input.
- The recent list (`flash/recent`) maps stored `modem` entries to the LTE kind.
- The flash wizard (0010) step 2 shows the same two cards.
