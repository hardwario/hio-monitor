# ADR-0166 — Show what application firmware is chosen and what runs in the device

- **Status:** Accepted
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (GUI feedback round 1, plan 0021)

## Context

A catalog ID (32 hex digits) says nothing to the user. The catalog list/info API needs a Cloud token (open question
15), but the public page `https://firmware.hardwario.com/chester/<id>` needs none and shows the firmware name and
version (e.g. `hio-chester-push-fm:v4.0.1`), the upload time and the SHA-256 of the HEX. CHESTER firmware answers
the shell command `info show` with, among others, `fw-name` and `fw-version`.

## Options considered

1. Catalog ID → name and version only.
2. Firmware currently in the device only.
3. Both (chosen).

## Decision

- **Chosen firmware (catalog ID):** after a valid ID is entered, the app fetches the public page (same network rules as
  ADR-0037, 1 MiB cap) and shows "hio-chester-push-fm v4.0.1 · uploaded 2026-09-07". The page's SHA-256 is checked
  against the downloaded HEX (`firmware.checksum_mismatch`). If the page cannot be read or parsed, the summary says
  "Unknown firmware" and flashing is still allowed (the HEX download is authoritative, ADR-0037).
- **Local `.hex`:** file name, size and address range as today (no name/version detection).
- **Firmware in the device:** when the tab's `app` console is attached, the app runs `info show` once per session as a
  silent shell query (plan 0021, ADR-0170) and shows "In the device: hio-chester-push-fm v4.0.1" on the Application
  card; it is refreshed after a successful flash. Nothing is shown when the query fails or the target has no
  console (`lte`).
- The parsers live in `libs/firmware` (catalog page) and `libs/device` (`info show`), both bounded and fuzz-tested.

## Consequences

- Users see what they flash and what they replace.
- Parsing an HTML page is fragile; the parser is isolated, tolerant and covered by a fixture. A JSON endpoint from the
  new HARDWARIO system replaces it when available (open question 15).
