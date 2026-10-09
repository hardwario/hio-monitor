# ADR-0032 — Device profiles are compiled-in C++ data

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M1 planning session)

## Context

"New device support = new device profile" (architecture). A profile describes a device: its targets
(MCUs), J-Link device names, RTT buffer names, console rules (e.g. CHESTER's `#`-prefixed log lines),
default speed/latency, voltage threshold, and later UICR/PIB layout, wizard assets and catalog endpoints.

## Options considered

1. Compiled-in C++ data registered in a `ProfileRegistry` (chosen).
2. JSON resource files (embedded, overridable from a user directory) with schema validation.

## Decision

Profiles are `const` C++ structs (`DeviceProfile`, `TargetProfile`) in `libs/device/src/profiles/`,
registered in `hio::device::ProfileRegistry`. Adding a device = one new file + one registration line +
its unit tests. Profiles contain data only; behaviour differences are expressed by explicit, typed
fields (e.g. `bool hashPrefixIsLog`), never by code in the profile.

## Consequences

- Type-checked, no parsing or schema versioning, trivially testable.
- Adding a device needs a new release. Loading external profile files can be added later by a new ADR
  (the struct layout then becomes the schema).
