# ADR-0061 — Tab restore without connecting; responsive breakpoints

- **Status:** Accepted (details of ADR-0043)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 planning review)

## Context

ADR-0043 defines device tabs, the per-tab rail and the device bar. Two details were left for review:
- what happens to the tabs at the next start;
- how the layout adapts to small windows (laptops at 125–150 % scaling, split screens) and to longer Czech texts.

## Options considered

- Restore: **restore Disconnected (chosen)** / restore and auto-connect / never restore.
- Layout: **1100 / 1000 / 960 × 600 (chosen)** / a smaller minimum of 800 × 560 / no rail collapse.

## Decision

- **Tab restore:**
  - On quit the tab list is saved as `ui/tabs`: a JSON list of `{profile, target, sn}`, at most 8, in tab order,
    with the current tab index.
  - At start, with `ui/restoreTabs = true` (the default), the tabs are recreated in the *Disconnected* state.
    They never connect automatically.
  - A tab whose probe is not present shows the normal Disconnected state. Its Connect fails with
    `probe.not_found`, and it offers "Choose another probe".
  - With the setting off, or on an empty or invalid list, the app starts with one *Choose device* tab. An invalid
    list is logged and ignored.
- **Responsive layout** (in logical pixels, so independent of DPI):
  - Minimum window size **960 × 600**; default 1280 × 800; geometry remembered.
  - Window width **< 1100 px**: the rail collapses to icons only (tooltips carry the labels). At ≥ 1100 px it shows
    icon + label.
  - Console page width **< 1000 px**: the shell/log split stacks vertically (shell on top); the user can also choose
    this layout explicitly.
  - Toolbars move overflowing actions into their ⋯ menu instead of clipping. This must hold at 960 px in Czech.

## Consequences

- Restoring never touches hardware at start, so a crash on connect cannot loop.
- Layout tests at 960 × 600 in Czech (geometry assertions) are part of the M3 plans.
- The mockups show the collapsed rail at < 1100 px.
