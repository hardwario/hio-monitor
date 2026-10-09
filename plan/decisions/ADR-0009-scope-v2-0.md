# ADR-0009 — v2.0 scope: RTT, BLE, flashing; CHESTER via extensible profiles

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (bootstrap planning session)

## Context

Need a bounded first production release that fully replaces the old app.

## Decision

v2.0 supports J-Link RTT console + log, BLE NUS shell and flashing for **CHESTER** (nRF52840 app MCU + nRF91 LTE MCU). Device specifics live in a `DeviceProfile` abstraction so more devices can be added later. Serial console and other devices are out of scope for v2.0.

## Consequences

- Generic code must not hard-code CHESTER values; they belong in the profile.
