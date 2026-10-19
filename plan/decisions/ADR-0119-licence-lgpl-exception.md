# ADR-0119 — HARDWARIO licence with an LGPLv3 §4 exception for Qt

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M7 planning session)

## Context

`LICENSE` is `LicenseRef-HARDWARIO-5-Clause`. Its clause 5 forbids reverse engineering and modification of the
binaries. The app is a "Combined Work" with Qt under LGPLv3 (ADR-0002). LGPLv3 §4 requires that the terms of the
combined work permit modification of the work for the customer's own use and reverse engineering for debugging such
modifications, and that Qt can be replaced by a modified version. As written, the two conflict.

## Options considered

1. **Keep the HARDWARIO licence and add an explicit exception for the LGPL-required rights (chosen).**
2. Relicense the app under MIT or Apache-2.0 (like py-hardwario).
3. Leave it to legal and block the release without a direction.

## Decision

- `LICENSE` gets an added paragraph (wording to be approved by HARDWARIO's legal counsel) stating that, to the
  extent required by LGPLv3 §4 for libraries linked into the application (Qt), clause 5 does not prohibit modifying
  the work for the user's own use, replacing those libraries, or reverse engineering for debugging such
  modifications.
- **The legal sign-off is a release gate:** no v2 release is published before the amended `LICENSE` is approved
  and committed. 0015 tracks it as a checklist item, not as code.
- Packages ship `LICENSE`, the Qt LGPLv3 + GPLv3 texts, a written offer / link for the Qt source of the exact Qt
  version used, and all other third-party notices (ADR-0048 fonts, Material Symbols, CLI11, replxx, miniz) in a
  `licenses/` directory; Settings › About shows the same files (0006, 0016).
- Qt stays dynamically linked and replaceable in every package format (ADR-0002, ADR-0022): Windows and AppImage keep
  Qt DLLs/`.so` next to the binary, the .deb under `/opt` (ADR-0121), macOS in `Contents/Frameworks`.

## Consequences

- The text change is a legal task, outside this repository's engineering work, and can block the first release.
- Replacing Qt in the signed macOS bundle invalidates its signature; this is accepted (the user re-signs ad hoc).
