# ADR-0044 — Qt Quick Controls style: Material (Dense) with an own Theme singleton

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M3 GUI planning session)

## Context

M0 uses the default style and defers styling to M3. We need light + dark themes, consistent look on all OSes,
and a dense layout for a developer tool. LGPL constraint (ADR-0002): only Community modules.

## Options considered

1. Basic style + fully own components.
2. **Material style (chosen).**
3. Fusion / platform-native styles.

## Decision

- Style **Material**, variant **Dense**, set at compile time (`QQuickStyle::setStyle("Material")` before the
  engine loads; `qtquickcontrols2.conf` with `Variant=Dense`). Material is part of Qt Quick Controls (LGPL).
- `Material.theme` bound to the Theme setting (Light / Dark / System → `Material.System`),
  `Material.accent` / `Material.primary` / `Material.background` / `Material.foreground` set from our tokens.
- An own **`Theme` QML singleton** (`HardwarioMonitor/Theme`) holds all design tokens (colours, spacing,
  radii, fonts, log-level colours) for both themes; components never hard-code colours.
- Thin wrapper components (`HButton`, `HToolButton`, `ErrorPanel`, …) in `app/gui/qml/components/` encapsulate
  Material specifics so the style could be changed later in one place.

## Consequences

- Fast to build, consistent across OSes. Material defaults (elevation, ripple, large paddings) are toned down
  through Dense and our wrappers.
- Console views do not use Material text controls (see ADR-0045).
