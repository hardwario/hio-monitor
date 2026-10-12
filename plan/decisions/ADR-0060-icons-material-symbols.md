# ADR-0060 — Icons: Material Symbols SVG subset

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 planning review)

## Context

The GUI needs about 40 icons: rail, toolbar, device bar, banners, flash kinds and maintenance. The style is Qt Quick
Controls Material (ADR-0044). Icons must follow the theme colours (ADR-0047) and stay sharp at high DPI.

## Options considered

1. **Material Symbols (Google, Apache 2.0) — chosen.**
2. Lucide (ISC).
3. Phosphor (MIT).

## Decision

- Material Symbols **Outlined**, weight 400, grade 0, optical size 24, used as individual **SVG files**. There is no
  icon font, so no font-ligature dependency.
- Only the icons we use are copied into `app/gui/assets/icons/<name>.svg` from a pinned upstream release; the
  version and the source URL are recorded in `app/gui/assets/icons/README.md`. SVG is text and needs no LFS.
- Icons are rendered with `IconImage` / `icon.source` + `icon.color` from `Theme`, so they are recoloured per theme.
- Every icon is named after its function (`flash.svg`, `console.svg`, `reset.svg`), not after the upstream glyph,
  so a later set swap touches only the files.
- The Apache 2.0 licence text is listed under Settings → About → Licences.

## Consequences

- One consistent look with the Material style; the subset is small (≈ 40 files, < 100 KB).
- Adding an icon means copying one SVG from the same pinned release and updating the README list.
