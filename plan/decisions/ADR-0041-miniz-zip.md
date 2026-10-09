# ADR-0041 — miniz for reading zip files

- **Status:** Accepted (dependency approval pending with plan 0003)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M2 planning session)

## Context

LTE bundles and modem firmware are zip files (ADR-0040). Qt has no public zip API (`QZipReader` is private).

## Options considered

1. miniz (MIT, small C library) via CPM (chosen).
2. minizip-ng (zlib license, larger, more dependencies).
3. Qt private `QZipReader` (no compatibility promise, problematic with distribution Qt).

## Decision

miniz, pinned by CPM to an exact tag + SHA-256 (ADR-0010), used **read-only** and only inside `libs/firmware`
behind an RAII `ZipReader`. Limits: 64 entries, 128 MiB uncompressed per entry, compression ratio ≤ 200; entry
names with `..`, absolute paths or drive letters are rejected. miniz warnings are compiled as a third-party
target (warnings not promoted to errors for its sources only).

## Consequences

- One small additional dependency with a permissive license.
- C API calls are confined to one adapter file; malformed and malicious archives are covered by tests.
