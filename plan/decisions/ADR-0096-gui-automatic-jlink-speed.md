# ADR-0096 — GUI: automatic J-Link speed switch in the Advanced connection popover

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (merge of M1 round 6 / ADR-0090 into M3)

## Context

ADR-0090 made the J-Link speed automatic by default (`SessionOptions::speedKhz` = `std::nullopt`,
`TargetProfile::defaultSpeedKhz` = `std::nullopt`); a fixed kHz value is still possible. ADR-0095 scales the RTT
attach deadline for fixed speeds below 1000 kHz and warns below 125 kHz. The M3 GUI shell (plan 0004) offered
only a kHz spin box (5–50000) in the ⋯ Advanced popover of the connection bar, stored in the settings
`jlink/speedKhz` (global default) and per probe.

## Options considered

1. **"Automatic speed" switch + kHz box (chosen).**
2. A combo box: Auto / 1000 / 4000 / 12000 kHz / Custom….
3. Auto only in the GUI; fixed speeds only in the CLI.

## Decision

- The Advanced popover shows an **Automatic speed** switch, **on by default**. While it is on, the kHz spin box
  is disabled (it keeps the last fixed value for when the switch is turned off). Turning it off uses the spin
  box value, which starts at 4000 kHz if no fixed value was stored before.
- For a fixed speed below 125 kHz, the popover shows the ADR-0095 warning (`device::slowSpeedWarning`, the
  same translated backend text the CLI prints).
- Settings: `jlink/speedKhz` and the per-probe speed store **0 for automatic**. A missing key also means
  automatic. Values outside 5–50000 are treated as automatic. Existing stored fixed values stay fixed,
  including the old explicit 2000 kHz default: no migration.
- The ViewModel exposes `autoSpeed` (bool) and `speedKhz` (int, the fixed value). `SessionOptions::speedKhz`
  is `std::nullopt` when `autoSpeed` is true. There is no logic in QML (ADR-0003).
- Plan 0004 (GUI shell), § connection bar / Advanced popover, is amended accordingly.

## Consequences

- GUI and CLI behave the same by default (auto, as pylink).
- One more control in the popover; EN + CS texts.
