# ADR-0092 — `Settings::remove` for whole groups

- **Status:** Accepted (amends feature 0001 / ADR-0025; added on `v2/m2-flashing` for feature 0004)
- **Date:** 2026-10-12
- **Decided by:** Matej Strnad

## Context

The M3 GUI (feature 0004) stores per-device and per-tab data in settings groups (e.g. a named device) and
must be able to forget them. `hio::Settings` (ADR-0025) only reads and writes single keys.

## Options considered

1. **`Settings::remove(group)` removing a group and every key under it (chosen).**
2. Writing default values over each key (leaves stale keys; the GUI would have to know every key).
3. Direct `QSettings` access in the GUI (bypasses read-only/schema rules).

## Decision

- `[[nodiscard]] Status remove(std::string_view group)` removes the group and all keys under it (a missing
  group is no error). Like the other writers: read-only (newer schema) → `settings.newer_schema`, not writable →
  `io.write_failed`; written on `sync()` or destruction.
- An empty group or the `meta` group (schema version) → `usage.invalid_argument`.

## Consequences

- Callers can forget whole objects without knowing their keys.
- Removing is irreversible once synced; callers confirm in the UI where that matters.
