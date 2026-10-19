# ADR-0132 — J-Link firmware update is offered in the GUI

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M8 planning session)

## Context

Open question 19. The old app offered a J-Link firmware update when the probe firmware was outdated (0002). v2 has the
backend (`DeviceSession::firmwareStatus()`, `updateFirmware()` via `JLINKARM_UpdateFirmwareIfNewer`, 0002/M1), the CLI
`hio jlink update-firmware [-n SN] [--yes]`, the doctor check `probe-firmware` (0007) and the troubleshooting topic
`probe-firmware` (0009), which tells the user to run the CLI.

## Options considered

1. **GUI entry points in M8 (chosen).**
2. Keep CLI + hint only; button after v2.0.

## Decision

- A shared GUI action "Update J-Link firmware" (`ProbeFirmwareUpdate` operation in the GUI layer, calling the existing
  backend operation on the probe's owner thread) is offered in three places:
  1. the device bar / console banner when `firmwareStatus()` is `Outdated` after connect (warning banner, [Update]
     [Not now]; "Not now" hides it for this probe SN until the app restarts);
  2. the doctor result row `probe-firmware` (Fix button);
  3. the troubleshooting topic `probe-firmware` (replaces the `CommandBlock` step with a button; the CLI command stays
     shown as an alternative).
- **Confirmation dialog** names the probe (product, SN), current and new firmware strings, and warns "Do not unplug the
  J-Link during the update (about 10–30 s)". Esc = Cancel, no default button.
- The update needs exclusive use of the probe: an open session on that probe is detached first (RTT stopped, tab shows
  "Updating J-Link firmware…"), then the probe is re-opened and the session re-attached. With MCP holding the probe
  (ADR-0105) the action is disabled with the tooltip "Used by an AI assistant".
- `Newer` (probe firmware newer than the library) is shown as information only; no downgrade is offered.
- Errors reuse `probe.*` codes; a failure after the update started maps to the new `probe.firmware_update_failed`
  ("The J-Link firmware update did not finish." / "the probe was unplugged or lost power" / "Reconnect the J-Link and
  try again; if it no longer appears, run J-Link Commander, which can repair the firmware.").
- MCP gets no tool for it (ADR-0099 unchanged).

## Consequences

- Open question 19 is resolved; the legacy parity row "J-Link firmware outdated → offer firmware update" is met.
- 0004/0009 get amendments (banner, topic step).
