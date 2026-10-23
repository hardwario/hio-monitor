# ADR-0164 — Memory writes also exclude the SCS and FPB; `memory-write` is documented as code execution (amends ADR-0160)

- **Status:** Accepted
- **Date:** 2026-10-23
- **Decided by:** Matej Strnad (M6, focused review of the 0013 fix round, finding F1)

## Context

ADR-0160 excludes the NVMC from writes and states that the PIB can only be lost through erase/recover. The focused
review showed that RAM and the Private Peripheral Bus stay writable, so a client with only `memory-write` can make
the CPU run its own code (vector table in RAM via `SCB->VTOR` + a pended interrupt via `NVIC->STIR`/`ISPR`, PC via
`DCRSR`/`DCRDR`, FPB remap, or an overwritten stack + `go`), and that code can erase the PIB.

## Options considered

1. **Also deny the System Control Space and the FPB, and document the remaining risk.**
2. Require `memory-write` + `uicr` for `write_memory`.
3. Only document.

## Decision

Option 1.
- `writeDenied` additionally contains the System Control Space `[0xE000E000, 0xE000F000)` and the FPB
  `[0xE0002000, 0xE0003000)` on both CHESTER targets.
- The stack/RAM path cannot be closed without banning RAM writes. The `write_memory` tool description, the CLI
  `mem write` help and the consent text (0014) state that writing memory can make the device run arbitrary code,
  which can also erase its flash and the PIB.
- ADR-0160's sentence "the PIB can only be lost through the explicit erase/recover paths" is corrected to "the PIB
  cannot be lost through a direct memory write; `memory-write` can still run code on the device".

## Consequences

- `memory-write` is a high-risk consent category; users are told so where they grant it.
