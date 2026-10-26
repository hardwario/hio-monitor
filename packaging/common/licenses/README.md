# Third-party licence texts (ADR-0119, plan/features/0015 A4)

Licence texts of third-party components shipped in every package that are not already stored next to their files
elsewhere in the repository. `packaging/common/licenses.cmake` installs them, together with `LICENSE`, the font
licences (`app/gui/assets/fonts`), the Material Symbols licence (`app/gui/assets/icons`), a generated `Qt-SOURCE.txt`
and the generated index `THIRD-PARTY-NOTICES.txt`, into the `licenses/` directory of the installed tree.

| File | Component | Source |
|---|---|---|
| `Qt-LGPL-3.0.txt` | Qt 6 (LGPL-3.0-only) | FSF text, Debian `/usr/share/common-licenses/LGPL-3` |
| `GPL-3.0.txt` | GPL-3.0, referenced by the LGPL-3.0 | FSF text, Debian `/usr/share/common-licenses/GPL-3` |
| `CLI11-BSD-3-Clause.txt` | CLI11 2.7.2 | `LICENSE` of the pinned release archive (`cmake/HioDeps.cmake`) |
| `replxx-LICENSE.md` | replxx (commit 1f149bf) | `LICENSE.md` of the pinned archive |
| `miniz-MIT.txt` | miniz 3.1.2 | `LICENSE` of the pinned release archive |

When a dependency is added or upgraded (`cmake/HioDeps.cmake`), update its row in `packaging/common/licenses.cmake`
and its text here; the CTest `Packaging.LicenceTableCoversCpmDependencies` fails while a CPM dependency has no row.
