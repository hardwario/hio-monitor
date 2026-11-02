# ADR-0205 — Grayscale text antialiasing for the whole GUI

- **Status:** Accepted
- **Date:** 2026-11-02
- **Decided by:** Matej Strnad (plan [0028](../features/0028-gui-feedback-round-2.md) U1, after the Phase C findings)

## Context

Customer feedback F3: in Czech, "odešle" in Settings → Console showed a gap between "š" and "l" and a reddish "l".
Phase C of plan 0028 found that the glyph metrics are correct and that Qt Quick draws all text with *subpixel* (LCD)
antialiasing when the desktop asks for it (fontconfig `rgba`, ClearType): the default `Text.QtRendering` uses
subpixel distance-field antialiasing and `NativeRendering` the platform's LCD glyphs. The colour fringes of thin stems
read as gaps and colour, worst on a panel whose subpixel order differs from the setting and in zoomed screenshots.
The defect is not limited to one label.

## Options considered

1. Per component: `Text.CurveRendering` on the affected label (fixes one label only).
2. **Grayscale distance-field antialiasing for the whole app (`QSG_DISTANCEFIELD_ANTIALIASING=gray`) (chosen).**
3. `QQuickWindow::setTextRenderType(CurveRendering)` globally (also changes hinting and glyph shapes).

## Decision

- `app/gui` `main()` sets `QSG_DISTANCEFIELD_ANTIALIASING=gray` before the `QGuiApplication` is created, unless the
  variable is already set in the environment (a user or a developer may still choose another mode).
- Text keeps `Text.QtRendering` (distance fields); no font file or render type changes.
- The Phase C harness (`tst_text_rendering.qml`) checks that the grayscale render types have no coloured pixels.

## Consequences

- Text is grayscale-antialiased on every platform: no colour fringes, slightly softer text on LCD panels whose
  subpixel setting matches.
- Explicit `Text.NativeRendering` items (none in the app) would still use the platform's LCD glyphs.
- The CLI is not affected (no Qt Quick).
