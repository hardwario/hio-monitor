# ADR-0054 — Firmware catalog in the M3 GUI: ID field only

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 GUI planning session)

## Context

Catalog browsing needs a HARDWARIO Cloud token (open question 15); ADR-0037 provides download by ID with a cache.

## Options considered

1. **ID field + recent list + link to the web catalog in M3 (chosen).**
2. Design a catalog browser and token storage now.

## Decision

The Flash page accepts a 32-hex-digit firmware ID (downloads via `FirmwareCatalog`, progress shown in the source
card, cached per ADR-0037), lists recent sources, and offers "Open firmware catalog" (external browser).
Browsing and token storage remain open question 15 (M4+).

## Consequences

- No secrets handled by the GUI in M3.
