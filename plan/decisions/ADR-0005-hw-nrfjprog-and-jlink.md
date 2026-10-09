# ADR-0005 — Hardware access the same way as py-hardwario

- **Status:** Superseded by ADR-0015 (programming) — RTT part still valid
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (bootstrap planning session)

## Context

py-hardwario works reliably: it uses Nordic nrfjprog (flash/erase/reset/recover/UICR) and SEGGER JLinkARM (RTT). Python must not be used at runtime. nrfjprog is deprecated by Nordic.

## Decision

Implement natively in C++, mirroring py-hardwario: **nrfjprog DLL** for programming operations (sector erase → program → verify → reset), **JLinkARM DLL** for RTT (buffers by name, reconnect, reset detection). Both behind interfaces (`IProgrammer`, `IConsoleTransport`) so nrfjprog can be replaced later (see open-questions #1).

## Consequences

- Proven sequences and error mappings can be ported 1:1 (see plan/0002).
- A `DeviceSession` must own the probe exclusively because both DLLs drive the same J-Link.
- Replacing nrfjprog later only affects `libs/hw/nrfjprog`.
