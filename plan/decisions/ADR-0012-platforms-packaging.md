# ADR-0012 — Supported platforms and packaging

- **Status:** Accepted (amended by ADR-0118: v2.0 Linux packages are AppImage + .deb, Flatpak deferred, .deb requires Ubuntu 24.04+; Windows format MSI per ADR-0117)
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (bootstrap planning session)

## Context

Must run on Linux, Windows, macOS.

## Decision

Minimum: Windows 10 22H2+/11 x64, macOS 13+ (universal), Ubuntu 22.04+/Fedora 40+ x86_64. Packages: Linux **AppImage, Flatpak and .deb**; macOS signed+notarized universal **DMG**; Windows signed installer (format: open-questions #8).

## Consequences

- Flatpak needs a design for USB and BLE access with runtime-loaded vendor libs (open-questions #7).
- CI builds packages on every tag; unsigned on PRs.
