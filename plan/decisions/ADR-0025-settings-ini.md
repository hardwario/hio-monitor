# ADR-0025 — Settings stored as INI files via QSettings

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M0 planning session)

## Context

The architecture uses `QSettings` with a versioned schema. Native formats differ per OS (registry,
binary plist), which makes support and reset harder.

## Options considered

1. `QSettings::IniFormat` on all OSes.
2. Native format per OS.

## Decision

Use **`QSettings::IniFormat`, user scope, on all OSes** (`HARDWARIO/Monitor.ini` under the platform
config directory), shared by GUI and CLI. A `meta/schemaVersion` key and an ordered list of migrations;
a file written by a newer schema is opened read-only and never overwritten.

## Consequences

- Users and support can read, attach and delete the file easily; the diagnostic report can include it.
- Behaviour is identical on every OS, and tests run on temp files.
- No secrets are stored in plain settings (future tokens need a separate decision).
