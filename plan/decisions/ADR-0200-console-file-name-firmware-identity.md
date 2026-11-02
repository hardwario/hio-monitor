# ADR-0200 — Console file names carry the firmware identity

- **Status:** Accepted (plan [0028](../features/0028-gui-feedback-round-2.md), approved 2026-11-02); amends the
  default file names of ADR-0128 and plan 0002
- **Date:** 2026-11-02
- **Decided by:** Matej Strnad (customer feedback, GUI feedback round 2; the details were decided with plan
  0028 on 2026-11-02, recommended options D1–D22)

## Context

A customer flashes and tests many CHESTER devices in a row on the same J-Link. Every session writes a console file
named `<profile>-<target>-YYYYMMDD-HHMMSS-<probe SN>.log` (plan 0002, ADR-0128), so all files differ only by the time
stamp and the user cannot tell which firmware a file belongs to. The file is opened before the device prints
anything (session start), so the firmware is not known when the name is chosen.

## Options considered

1. Keep the names; the firmware is visible only inside the file.
2. **Put the firmware name and version into the generated name once they are known (chosen).**
3. Let the user type a name per session.

## Decision

- Generated (default-mode and `--console-file-dir`) console file names become
  `<fw-slug>-<fw-version>-<UTC stamp yyyyMMdd-HHmmss>-<probe SN>.log` once the firmware is identified; the stamp
  stays UTC and the probe SN (BLE: sanitised id) stays last, so retention keeps recognising the session key.
- Firmware identity source: primary the CHESTER `info show` answer (firmware name / version); fallback a Zephyr boot
  banner only when it carries the *application* name and version (never the SDK banner such as
  `*** Booting nRF Connect SDK v2.8.0 ***`).
- Not identified → today's name stays. An explicit `--console-file` path is never renamed.
- The exact slug rule, the behaviour after a firmware change within a session, rename vs delayed open, `.partN`
  handling, CLI / BLE / MCP scope and the header marker are decided with plan 0028 (its decisions table, D1–D11,
  recommended options accepted 2026-11-02).

## Consequences

- `ConsoleFileSink` gains a rename step on the owning thread (J-Link worker, BLE thread); the identity travels there
  as a command, never through shared state.
- `sessionKeyOf()` must parse both the old and the new names (old files stay in users' directories).
- Tests that pin the file name format are extended, not replaced.
- The `FirmwareIdentity` data struct lives in libs/core (`hwio/core/firmware_identity.h`), because it travels through
  the libs/hw/ble command queue to the BLE console handler and libs/hw/ble must not depend on libs/device; the rules
  (parsing, slugs, file name base) stay in libs/device (review round 1, R8).
- Identity comparison (review round 1, R1): the device serial counts only when both identities have one; a change of
  the serial alone is reported as another device, not another firmware.
