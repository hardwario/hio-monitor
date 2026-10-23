# ADR-0160 — Memory reads are unrestricted; writes exclude the flash controller (NVMC)

- **Status:** Accepted (amended by ADR-0164: SCS and FPB also excluded; memory writes can still run code)
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (M6, after the independent review of 0013 phases A–C)

## Context

The review of 0013 found that `mem write` / `write_memory` could write the NVMC registers (nRF52840 at `0x4001E000`,
nRF9160 at `0x50039000`/`0x40039000`), which lie inside the writable peripheral range. ERASEALL, ERASEUICR or
ERASEPAGE would wipe flash, UICR and the PIB with only the `memory-write` consent, breaking ADR-0036 and ADR-0101.
At the same time reads were restricted to the profile ranges, so FICR and the ROM table could not be read (rttt
reads anywhere).

## Options considered

- Writes: **exclude the NVMC** / allow NVMC writes with `erase` + `uicr` consent.
- Reads: **unrestricted** / restricted to the profile ranges.

## Decision

- `TargetProfile` gains a `writeDenied` list (half-open ranges). CHESTER app: the NVMC block; lte: both NVMC aliases.
  A write that overlaps any denied range is `target.address_out_of_range` (CLI, MCP, `DeviceSession`), regardless of
  consent or `--yes`.
- Reads (`readMemory`, `mem read`, `read_memory`) accept any 32-bit address range that does not wrap; an unmapped
  address gives `target.memory_access_failed`.

## Consequences

- The PIB can only be lost through the explicit erase/recover paths (ADR-0161). Tests cover the NVMC task addresses.
