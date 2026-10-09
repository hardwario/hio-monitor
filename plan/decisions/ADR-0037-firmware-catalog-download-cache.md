# ADR-0037 — Firmware catalog: download by ID with a bounded cache (browsing deferred)

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M2 planning session)

## Context

py-hardwario and the old app download `https://firmware.hardwario.com/chester/{id}/hex` for a 32-hex-digit ID.
py-hardwario caches in `~/.hardwario/chester/cache` without validation, timeouts or atomic writes; the old app
re-downloads every time and ignores save errors. The list/detail API (`/chester/api/v1/firmware`) needs a
HARDWARIO Cloud Bearer token, which v2 has no place to store yet.

## Options considered

1. Download by ID + read-only list/info (requires token handling now).
2. Download by ID only; browsing decided with the GUI flash wizard (chosen).
3. Cache in py-hardwario's path to share it.

## Decision

- M2 implements download by ID only (`libs/firmware`, QtNetwork, owner thread, async, cancellable). HTTPS only,
  60 s transfer timeout, 32 MiB body cap, HTTP status checked (404 → `firmware.not_found_in_catalog`).
- Cache: `AppLocalDataLocation/firmware-cache/<id>.hex`, written with `QSaveFile` only after a complete download
  that parses as Intel HEX; hits are re-parsed (corrupt entries deleted); LRU by mtime, **100 files / 500 MiB**;
  `--no-cache` forces a download.
- Catalog browsing and Cloud token storage are an open question for M3/M4 (open-questions #15).

## Consequences

- No partial or corrupt cache hits, bounded disk use; the cache is not shared with py-hardwario.
- The GUI flash wizard cannot browse the catalog until a token decision is made.
