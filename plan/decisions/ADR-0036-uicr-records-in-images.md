# ADR-0036 — UICR records in firmware images are rejected unless explicitly allowed

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M2 planning session)

## Context

An application HEX may contain records in the UICR area (nRF52 `0x10001000`, nRF91 `0x00FF8000`). On nRF52 UICR
can only be erased as a whole page, which would also erase the Product Information Block (PIB, UICR + 0x80) holding
the serial number and claim token. nrfjprog's sector-erase mode handled this implicitly and silently.

## Options considered

1. Reject by default (`firmware.uicr_in_image`); `--allow-uicr` performs a read-modify-write of UICR that keeps
   the PIB bytes (chosen).
2. Always read-modify-write, silently.
3. Always reject (UICR writing out of scope, like PIB write).

## Decision

Option 1. With `--allow-uicr` (GUI: an explicit checkbox in M3) the workflow reads the full UICR, merges the image's
UICR bytes, keeps the PIB range from the device, erases UICR (where the target needs it) and writes the merged
content, then verifies it. An image writing into the PIB range itself is always rejected.

## Consequences

- The PIB can never be lost by flashing an application image.
- Firmware that legitimately configures UICR needs one extra flag; the error message tells the user about it.
