# ADR-0055 — Flash page: choose what to flash first

- **Status:** Accepted; kind list superseded by ADR-0174 (2026-10-20)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 GUI review of mockups)

## Context

ADR-0049 started the Flash page with the file and chose the target MCU afterwards. In review, the user wanted
the first step to be *what* is being flashed. That also lets the page filter the accepted files and explain
prerequisites (nRF Util) before a file is picked.

## Options considered

1. **Firmware kind cards: Application firmware / LTE firmware / LTE modem firmware (chosen).**
2. Target MCU only (app / lte) first; LTE file type still auto-detected.
3. Operation first (Flash / Erase / Recover / Reset).

## Decision

- Step **1 "What do you want to flash?"** has three radio cards:
  - **Application firmware**: application MCU; accepts a `.hex` file or a catalog ID.
  - **LTE firmware**: LTE MCU; accepts a `.hex` file, an LTE bundle `.zip` or a catalog ID.
  - **LTE modem firmware**: modem core via nRF Util; accepts `mfw_nrf91*.zip` only, with no catalog ID.
- Step **2 "Firmware file"** offers a drop zone, Browse (file dialog filtered to the kind), the catalog ID field (not for
  modem) and a Recent list filtered to the kind.
  - The input is still classified (ADR-0040). A file that belongs to another kind shows a hint,
    "This is LTE modem firmware — switch to LTE modem firmware?" [Switch]. The page never switches silently.
- Step **3 "Options"** offers "Halt the CPU after flashing" (not for modem) and "Allow writing UICR" (only when the image
  has UICR records, ADR-0036).
- A sticky bottom bar shows a summary ("Application firmware · app.hex") and **Flash**. When Flash is disabled, the bar
  shows the reason.
- Prerequisites are shown in step 1 as soon as the kind is chosen, e.g. nRF Util is missing for modem, with
  "Set path in Settings" and "How to install".
- The default kind is the tab's target: `app` → Application firmware, `lte` → LTE firmware. The last kind per tab is
  remembered for the session.
- The Maintenance section, progress and result handling stay as in ADR-0049.

## Consequences

- Flashing an MCU other than the tab's console target is normal. ADR-0057 defines how the session does it.
