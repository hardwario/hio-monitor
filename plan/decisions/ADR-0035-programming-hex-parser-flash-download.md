# ADR-0035 — Programming: own Intel HEX parser, J-Link flash download, read-back verify

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M2 planning session)

## Context

ADR-0015 moves programming from nrfjprog to the JLinkARM library. py-hardwario leaves HEX parsing, sector erase
and verification to nrfjprog (`erase_file(ERASE_SECTOR)`, `program_file`, `verify_file`), and the old app did the
same with no image checks and no progress. v2 needs image validation before the chip is touched (address ranges,
UICR records, family), byte-level progress and cancellation.

## Options considered

1. Own Intel HEX parser → `FirmwareImage`; write with `JLINKARM_BeginDownload` / `JLINKARM_WriteMem` /
   `JLINKARM_EndDownload` (SEGGER flash loader erases only the touched sectors); verify by reading back and
   comparing (chosen).
2. `JLINK_DownloadFile`: the library parses and programs the file. Least code, but coarse progress and no
   validation before programming.
3. Own NVMC driver (erase pages / write words through Nordic registers). Full control, per-family flash logic
   in our code, slower.

## Decision

Option 1. The parser lives in `libs/firmware` (bounded: 64 MiB file, 16 MiB data). `JLinkProgrammer` writes in
4 KiB chunks and verifies in 16 KiB chunks with progress and stop checks between chunks. Every program is followed
by a full read-back verify of the written ranges. The M2 Phase 0 spike must confirm that the flash loader
keeps UICR (and so the PIB) intact on nRF52840 and nRF91; if not, a new ADR replaces this one (e.g. option 3).

## Consequences

- Images are validated (ranges, UICR, overlaps) before the probe is used; precise errors with line/address.
- We own a small, heavily tested parser; the fake J-Link library needs a flash memory model.
- Progress and cancellation are fine-grained; verify roughly doubles the probe traffic (accepted for safety).
