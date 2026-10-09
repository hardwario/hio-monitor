# ADR-0039 — Recover for app and LTE MCUs; nRF91 disable-AP-protect image from py-hardwario

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M2 planning session)

## Context

A read-back protected (APPROTECT) Nordic MCU can only be reached after an erase-all through the CTRL-AP
("recover"). On nRF91 (and nRF52 revisions with hardware APPROTECT) the debug port closes again after reset unless
UICR.APPROTECT says "HwDisabled" **and** running firmware opens it; py-hardwario therefore programs
`resources/nrf91_disable_ap_protect.hex` after `recover` (only in `device nrf91 pib write`). `hardwario chester` has
no recover command.

## Options considered

- Scope: recover for app + lte (chosen) / lte only / none.
- Image: own source built offline / copy py-hardwario's hex (chosen) / emit the needed writes from C++.

## Decision

- New commands `hio chester app recover` and `hio chester lte recover` (confirmation per ADR-0038): CTRL-AP
  ERASEALL via J-Link CoreSight access, poll status (60 s), reset, reconnect; on targets whose profile says
  `Nrf91DisableApProtect`, program and verify the image, then reset and run.
- `flash` on a protected chip fails with `target.protected` suggesting recover; recover is **never** automatic.
- The image is copied verbatim from py-hardwario (MIT, © HARDWARIO a.s., commit `9ada1c6`, 8 231 bytes of Intel HEX
  text covering `0x0–0xBA4`) into `libs/device/resources/` and compiled in as a Qt resource. Its origin is documented
  next to it, and a unit test checks its SHA-256 and parsed layout.

## Consequences

- Locked devices can be recovered from the app; the PIB is lost by recover (stated in the confirmation).
- The image is a prebuilt binary-like artifact we cannot rebuild from source; replacing it needs a new ADR.
- The M2 spike must confirm the image keeps nRF9160 **and** nRF9151 debuggable after reset.
