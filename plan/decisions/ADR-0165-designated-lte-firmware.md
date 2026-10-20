# ADR-0165 — Designated LTE firmware pinned in the device profile

- **Status:** Accepted
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (GUI feedback round 1, plan 0021)

## Context

There is exactly one LTE firmware that HARDWARIO supports for CHESTER at a time. Today it is the LTE bundle
`hio-chester-lte-v1.7.0.zip` (`application.hex` + `modem.zip`) linked from the documentation
(`https://docs.hardwario.com/download/hio-chester-lte-v1.7.0.zip`, "LTE Modem over J-Link"). Users download it by
hand and then pick it in the app. The app should download it itself and offer a custom file only as an option.

## Options considered

1. Pinned in the device profile: URL, version and SHA-256 compiled in; a new LTE firmware ships with a new app
   release (chosen).
2. A JSON manifest hosted by HARDWARIO, read at run time (needs a new hosted file and its own trust model).
3. Pinned, plus an optional check for a newer version through a manifest later.

## Decision

- The CHESTER profile's `lte` target gets a **designated firmware** entry: display name, version (`1.7.0`), HTTPS URL,
  SHA-256 of the file and the expected kind (LTE bundle).
- The firmware is downloaded with the catalog downloader rules (ADR-0037: HTTPS only, 60 s transfer timeout, 32 MiB
  cap, owner thread, cancellable) into the firmware cache as `lte-<sha256>.zip`; the **SHA-256 is verified** before the
  file is cached or used. A mismatch is `firmware.checksum_mismatch` (new code) and the file is discarded.
- Cache hits are re-verified. The entry counts against the ADR-0037 cache limits.
- The GUI LTE card preselects it ("HARDWARIO LTE firmware 1.7.0 (recommended)"); the CLI uses it when
  `hio chester lte flash` gets no FILE (plan 0021).
- Updating the designated version = changing the profile entry in a release (reviewed like any code change).

## Consequences

- No hand download; integrity is checked against a pinned hash.
- A newer LTE firmware reaches users only with a new app release (acceptable: LTE firmware changes rarely).
- If HARDWARIO moves the file, the download fails with `network.download_failed`; "Use your own file…" still works.
