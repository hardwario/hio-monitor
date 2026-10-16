# ADR-0095 — RTT attach deadline scales with a fixed low J-Link speed

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (M1 HIL G1, finding H2)

## Context

The RTT attach deadline is a fixed 15 s (`RttTiming::attachDeadline`). HIL on a CHESTER nRF52840 with J-Link
Software 9.84 measured (connect itself took about 0.3 s at every speed):

| `--jlink-speed` | RTT control block found after |
|---|---|
| auto (default, ADR-0090) | < 2 s |
| 1000 kHz | < 3 s |
| 100 kHz | 12.8 s |
| 5 kHz | not within 15 s → `target.rtt_not_found` |

The library already limits its search to the MCU's RAM (`0x20000000–0x20040000`, 256 KiB) and scans it in
4 KiB steps, about one step per second at 100 kHz. The control block of the tested firmware was at
`0x2000CB30` (step 13). A search of the whole RAM therefore takes about 64 s at 100 kHz and about 6.4 s at
1000 kHz, and it scales inversely with the speed. Limiting the search range in the profile would not help,
because the address depends on the firmware build.

## Options considered

1. **Scale the deadline with the speed (chosen)**, with a cap.
2. Keep 15 s and warn below 1000 kHz.
3. Document only.

## Decision

- With `auto` (the default), and with a fixed speed of 1000 kHz or more, the attach deadline stays **15 s**.
- For a fixed speed below 1000 kHz, the deadline is `15 s × 1000 / kHz`, **capped at 60 s**: 30 s at
  500 kHz and 60 s at 250 kHz and below. `RttTiming::attachDeadline` is computed from the session speed. The
  connect budget, which includes the attach deadline (`device_session.h`), uses the same value.
- Below **125 kHz**, a full-RAM search can take longer than 60 s. In that case:
  - the CLI prints a warning before connecting: "At this J-Link speed the console may not be found within
    60 s. Use auto or 1000 kHz or more.";
  - the `target.rtt_not_found` detail names the speed.

  The GUI shows the same text next to the speed setting (M3 settings, when that page is built).
- The 125 kHz threshold is derived from the measurement above (≈ 64 s full search at 100 kHz). The user's
  decision named "about 25 kHz", which was a first estimate.

## Consequences

- Very slow speeds no longer fail by default for firmware whose control block sits low in RAM.
- At slow speeds, a session waits up to 60 s before reporting `target.rtt_not_found`.
- Reconnect after a reset uses the same deadline, so at slow speeds a reconnect can take longer.
