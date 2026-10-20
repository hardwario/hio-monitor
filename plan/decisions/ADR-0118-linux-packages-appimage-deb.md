# ADR-0118 — Linux packages in M7: AppImage and .deb; Flatpak deferred

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M7 planning session)

## Context

ADR-0012 listed AppImage, Flatpak and .deb. Open question 7: Flatpak must reach a J-Link library installed on the
host (ADR-0006), raw USB devices, BlueZ over D-Bus and `/proc` (ADR-0073); its value over AppImage + .deb is unclear
until this is proven.

## Options considered

1. **AppImage + .deb in M7, Flatpak later after a feasibility spike (chosen).**
2. AppImage + .deb + Flatpak in M7.
3. AppImage + .deb + .rpm.

## Decision

- M7 ships an **AppImage** (Ubuntu 22.04+ and other glibc ≥ 2.35 distributions, ADR-0121) and a **.deb**
  (Ubuntu 24.04+ / Debian 13+, ADR-0121).
- **Flatpak is deferred** to its own later plan, which starts with a spike: host J-Link library through
  `--filesystem=/opt/SEGGER:ro` or a portal, `--device=all` for USB, `--system-talk-name=org.bluez`, ADR-0073 checks
  in the sandbox, ADR-0058 single instance. Open question 7 stays open with that scope.
- Fedora users use the AppImage; .rpm is not planned.
- This **supersedes the packaging list of ADR-0012** for v2.0; the minimum OS versions of ADR-0012 are unchanged,
  except that the .deb requires Ubuntu 24.04+ (Ubuntu 22.04 is covered by the AppImage).

## Consequences

- Plan 0007's "Flatpak: checks report Skipped" notes become irrelevant for M7.
- Flathub distribution is not available for v2.0.
