# ADR-0047 — Themes: Light, Dark, System; proposed palette

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 GUI planning session)

## Context

UX principle 7 requires light and dark themes. The old app was dark only (GitHub-dark–like colours, accent
`#79B8FF`). No HARDWARIO brand guide was provided for the GUI.

## Options considered

1. **Light + Dark + System (default System) with a palette proposed in the design system (chosen).**
2. Dark only for now.

## Decision

- Setting `ui/theme` = `system` | `light` | `dark` (default `system`), switched live.
- Tokens are defined in [`plan/gui/design-system.md`](../gui/design-system.md); dark keeps the old app's accent and
  log colours for familiarity. Every text/background pair meets **WCAG AA (≥ 4.5 : 1)**; ratios are listed there.
- The palette is a proposal; changes during mockup review update the design system (no new ADR needed for
  individual colour values).

## Consequences

- Every component takes colours from `Theme`; Qt Quick Tests render key pages in both themes.
