# ADR-0048 — Bundled fonts: Inter + JetBrains Mono

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 GUI planning session)

## Context

The old app bundled Inter and Ubuntu Mono. System fonts differ per OS and some lack good Czech diacritics or
box-drawing characters used in device output.

## Options considered

1. **Bundle Inter (UI) + JetBrains Mono (console) (chosen).**
2. System fonts only.
3. Inter + Ubuntu Mono like the old app.

## Decision

- Inter (Regular, Medium, SemiBold) and JetBrains Mono (Regular, Bold), both **SIL OFL 1.1**, stored in
  `app/gui/assets/fonts/` via **Git LFS** (`*.ttf` / `*.otf` are already tracked in `.gitattributes`),
  with their licence files; loaded with `FontLoader` from the QML resources.
- Pinned versions recorded in `app/gui/assets/fonts/README.md` (source URL, version, SHA-256).
- Console font size is a setting (`console/fontSize`, 8–24 pt, default 10); Ctrl+= / Ctrl+- / Ctrl+0 zoom.

## Consequences

- Identical rendering on all OSes; ~1.5 MB added to packages; licence texts shipped in About / packaging.
