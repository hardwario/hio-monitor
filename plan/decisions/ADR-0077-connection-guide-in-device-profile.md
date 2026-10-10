# ADR-0077 — Connection guide (cabling steps) is part of the device profile

- **Status:** Accepted (extends ADR-0032)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M4 planning session)

## Context

The Connect and Flash wizards explain how to connect the J-Link to the device and how to power it. This is
specific to the device and to the target MCU. The architecture rule is "new device support = new device profile,
not changes scattered across the code". The exact CHESTER connector names and pin-1 marking are not documented in
this repository yet.

## Options considered

1. **The device profile carries the guide (chosen).**
2. A device-specific table inside the GUI wizard code.

## Decision

- `TargetProfile` (libs/device, ADR-0032) gains
  `std::span<const ConnectionGuideStep> connectionGuide`, where
  `struct ConnectionGuideStep { std::string_view id; std::string_view illustrationSlot; const char* titleSourceText;
  const char* bodySourceText; };` (texts marked with `QT_TRANSLATE_NOOP("hio::device", …)`). `DeviceProfile` gains
  `std::string_view powerHintSourceText`.
- `libs/device` stays free of QtGui: slot ids are strings; the images live in `app/gui/assets/wizard/` (ADR-0076).
- CHESTER texts are written neutrally for now ("Connect the J-Link flat cable to the programming connector of the
  application MCU."). The exact connector names, the cable type and the pin-1 marking are a **content task for
  HARDWARIO** that must be completed before 0009 is Done (open question 22).

## Consequences

- A new device adds its guide to its profile; the wizards do not change.
- A unit test checks that every guide step's illustration slot exists in the GUI slot table.
