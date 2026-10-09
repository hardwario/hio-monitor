# ADR-0040 — `lte flash` accepts a bundle, a modem firmware zip or a HEX file, detected by content

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M2 planning session)

## Context

py-hardwario `chester lte flash FILE`: a 2-entry zip must be `modem.zip` + `application.hex`; any other zip programs
nothing but reports success; a non-zip is programmed as an application image.

## Options considered

1. Bundle, plain Nordic modem firmware zip, or HEX, classified by content (chosen).
2. Bundle and HEX only, with an error for everything else.

## Decision

`classifyLteInput` (`libs/firmware`) inspects the file, not its extension:
- zip with exactly the top-level entries `modem.zip` and `application.hex` → **bundle**: modem firmware via nRF Util
  (ADR-0016), then the application via J-Link, in one command;
- zip with Nordic modem firmware markers (fixed from real `mfw_nrf91*` files in the M2 spike) → **modem only**;
- Intel HEX → **application only**;
- anything else → `firmware.invalid_image`. Nothing is ever reported as success without programming.

## Consequences

- Users can update just the modem with Nordic's release zip.
- Detection rules must be kept in sync with Nordic's modem zip layout; covered by tests with real file listings.
