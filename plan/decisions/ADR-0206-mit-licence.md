# ADR-0206 — v2 under the MIT licence

- **Status:** Accepted (supersedes the licence part of ADR-0119)
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad (RC preparation session)

## Context

ADR-0119 kept `LicenseRef-HARDWARIO-5-Clause` (use only with HARDWARIO hardware, no reverse engineering) and added
an LGPLv3 §4 exception for Qt, whose wording needed legal approval before the first release. Option 2 of ADR-0119,
relicensing under MIT or Apache-2.0 like py-hardwario, removes the conflict instead. The v2 history (orphan root
`4838dff`) contains only HARDWARIO a.s. work, so no other contributor has to agree. v2 is moving to a new repository.

## Options considered

1. **MIT for all of v2 (chosen).**
2. Keep ADR-0119 (HARDWARIO licence + LGPL exception, legal sign-off as a release gate).
3. Apache-2.0 (patent grant, longer notice requirements).

## Decision

- `LICENSE` is the MIT licence, `Copyright (c) 2026 HARDWARIO a.s.`; SPDX `MIT` (Debian `Expat`) in the package
  metadata. It applies to every v2 commit, including those made before this ADR under the HARDWARIO licence.
- The history in the current repository is not rewritten. When v2 moves to its new repository, its history may be
  rewritten there so that every commit carries the MIT `LICENSE` (a separate decision of that move).
- Still in force from ADR-0119: the `licenses/` directory with the Qt LGPLv3 + GPLv3 texts, `Qt-SOURCE.txt` (written
  offer for the exact Qt sources) and all other third-party notices; Qt stays dynamically linked and replaceable in
  every package format; Settings › About shows the same files.
- The LGPL exception and its legal sign-off gate are dropped. The legal review of `Qt-SOURCE.txt` stays a gate.
- The old Qt application (branch `main`) keeps its licence.

## Consequences

- One release gate less; MIT is compatible with LGPLv3 dynamic linking and with every bundled component (CLI11 and
  replxx BSD-3-Clause, miniz MIT, Inter and JetBrains Mono OFL-1.1, Material Symbols Apache-2.0).
- Anyone may use, modify and redistribute v2, also with non-HARDWARIO hardware; the HARDWARIO name and trademarks are
  not licensed by MIT.
- Releases must keep the MIT notice (copyright line + permission text) in `licenses/LICENSE`.
