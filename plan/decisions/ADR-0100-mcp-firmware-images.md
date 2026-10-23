# ADR-0100 — MCP flash tools take local paths or catalog ids; no upload endpoint

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (M6 planning session)

## Context

rttt offers `POST /upload` (≤ 64 MiB into a temp dir) so that an AI on another machine can send an image. Our
server listens only on 127.0.0.1 (ADR-0102).

## Options considered

1. **A local absolute path or a firmware catalog id (chosen).**
2. Also an upload endpoint.
3. Also base64 images inline (≤ 2 MiB).

## Decision

- `flash`, `flash_lte`, `write_flash` and `write_memory` accept `path`: an **absolute** path on this machine to
  a regular file, at most 64 MiB, with the M2 input detection (ADR-0035, ADR-0040).
- `flash` alternatively accepts `catalog_id`, downloaded through the bounded cache (ADR-0037).
- `write_memory` and `write_flash` also accept `data_hex` (≤ 64 KiB decoded).
- There is no HTTP endpoint other than `/mcp`.
- `read_memory` can write to a file instead of inline (`to_file`, ≤ 16 MiB): an absolute path, or a bare name,
  which goes to `<AppCacheLocation>/mcp/`. An existing file is never overwritten (`io.file_exists`).

## Consequences

- Smaller attack surface. An AI in a container must mount the firmware directory or use the catalog.
- Every file access is logged in the activity list with its path and size (ADR-0105).
