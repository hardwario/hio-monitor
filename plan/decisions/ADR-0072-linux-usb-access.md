# ADR-0072 — Linux USB access for J-Link: detect and show the commands

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M4 planning session)

## Context

On Linux a J-Link is usable by a normal user only with a udev rule. Without it, opening the probe fails as
`probe.open_failed`. Neither the old app nor py-hardwario handles this.

## Options considered

1. **Detect missing access, show the rule and the exact commands to copy (chosen).**
2. Install the rule with `pkexec` (privileged code path, polkit dependency, problems in Flatpak).
3. Link to documentation only.

## Decision

- Detection (`libs/diag`, Linux only, no vendor calls): for every USB device in `/sys/bus/usb/devices/*` with
  `idVendor` = `1366`, check read/write access to `/dev/bus/usb/<busnum>/<devnum>`. No access → check Failed with
  the new error `usb.access_denied`. No SEGGER device → Skipped ("No J-Link connected").
- The wizard shows the rule
  `SUBSYSTEM=="usb", ATTR{idVendor}=="1366", MODE="0666", TAG+="uaccess"`
  and the commands, each with [Copy]:
  ```
  echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1366", MODE="0666", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/99-jlink.rules
  sudo udevadm control --reload-rules && sudo udevadm trigger
  ```
  then "Unplug and reconnect the J-Link" and [Check again].
- The app never runs anything as root. The Linux packages (M7) install the rule themselves.
- `probe.open_failed` on Linux runs this check before suggesting anything else.

## Consequences

- One new error code `usb.access_denied` (category Probe, wizard FirstRunSetup).
