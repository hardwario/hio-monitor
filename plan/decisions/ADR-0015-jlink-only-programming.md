# ADR-0015 — Replace nrfjprog with the SEGGER J-Link library

- **Status:** Accepted (supersedes the programming part of ADR-0005)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad

## Context

ADR-0005 mirrored py-hardwario: nrfjprog DLL for programming, JLinkARM DLL for RTT.
nrfjprog is deprecated by Nordic, and two libraries driving the same J-Link probe caused
conflicts in the old app. The J-Link SDK (checked in `include/jlink/JLinkARMDLL.h` on `main`)
already offers all the needed operations:

| nrfjprog operation | J-Link SDK |
|---|---|
| program + sector erase (UICR preserved), verify | `JLINK_BeginDownload` / `JLINK_WriteMemEx` / `JLINK_EndDownload`, `JLINK_DownloadFile`, read-back verify |
| erase all | `JLINK_EraseChip` / `JLINK_FLASH_EraseChip` |
| reset / halt / go | `JLINKARM_Reset`, halt, go |
| recover (APPROTECT) | `JLINK_CORESIGHT_WriteAPDPReg` (Nordic CTRL-AP ERASEALL) or J-Link built-in unlock |
| probe list, low-voltage diagnosis | `JLINK_EMU_GetList`, `JLINK_GetHWStatus` |
| UICR / PIB read & write | memory read / flash download |

## Options considered

1. J-Link library only (chosen).
2. nRF Util as a subprocess — official successor, but external process + JSON parsing, and it
   still drives the J-Link underneath (competes with RTT).
3. Keep nrfjprog — least work, deprecated.

## Decision

Implement `IProgrammer` on the **SEGGER JLinkARM library only** (the same runtime-loaded library
used for RTT). The *behaviour* of py-hardwario is still the reference (sequence
reset → halt → sector erase → program → verify → reset → go, family check, error mapping,
recover then disable-AP-protect image on nRF91), but implemented with J-Link calls.
nRF91 modem firmware is the only exception — see ADR-0016.

## Consequences

- One vendor library, one probe owner (`DeviceSession`) for both RTT and programming.
- We own the Nordic-specific logic (family detection, recover / APPROTECT per family, UICR
  handling, nRF91 → nRF9151 naming); it must be covered by GMock tests and hardware tests.
- **A hardware spike is required before the M2 plan is approved**: verify on CHESTER
  nRF52840 and nRF9160/nRF9151 that sector-erase programming preserves UICR, that recover
  works, and measure flashing speed vs. nrfjprog.
- New Nordic chips depend on SEGGER's J-Link device support (minimum J-Link version will be
  defined and checked at runtime).
