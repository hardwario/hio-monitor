# ADR-0028 — Own minimal declarations of the JLinkARM API

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M1 planning session)

## Context

The JLinkARM library is loaded at runtime (ADR-0006), so we need typed function pointer declarations.
The old app committed SEGGER's `JLinkARMDLL.h` from the J-Link SDK, whose license very likely does not
allow redistribution in a public repository. Requiring the SDK header at build time would block CI and
contributors who are not SDK licensees.

## Options considered

1. Own minimal declarations of only the functions and structs we use (chosen).
2. Vendor SEGGER's `JLinkARMDLL.h` (complete API, license risk).
3. Take the header from the developer's J-Link SDK installation (blocks CI and contributors).

## Decision

`libs/hw/jlink/src/jlink_abi.h` (private to the adapter) declares only the functions, constants and
structs we call, written from SEGGER's public documentation (UM08002) and pylink's public bindings.
No SEGGER header is committed. Symbols are resolved by name at load time; a missing symbol is a
`dependency.jlink_load_failed` error naming the symbol. `reinterpret_cast` of resolved symbols happens
only in this file's companion loader (CLAUDE.md: vendor adapters only).

## Consequences

- No license issue; the build needs nothing from SEGGER.
- We own the correctness of signatures and struct layouts (calling convention on Windows, packing):
  covered by `static_assert`s on struct sizes, the fake J-Link library in tests and hardware tests.
- Every new J-Link function used later is added to this file deliberately (small, reviewable list).
