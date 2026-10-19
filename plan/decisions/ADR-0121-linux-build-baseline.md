# ADR-0121 — Linux build baseline: Ubuntu 22.04 container; .deb with private Qt under /opt

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M7 planning session)

## Context

ADR-0011 uses the system libstdc++ (≥ 13); ADR-0021 left the glibc baseline of distributable packages to M7.
Ubuntu 22.04 has glibc 2.35 and libstdc++ 12. Ubuntu 24.04 ships Qt 6.4, older than Qt 6.8 (ADR-0002).

## Options considered

Baseline: (1) build in an Ubuntu 22.04 container (chosen); (2) drop 22.04 and build on 24.04; (3) build on 22.04 and
ship a private libstdc++ in both formats.

.deb Qt: (1) private Qt 6.8 under `/opt` (chosen); (2) depend on distribution Qt (only Ubuntu 26.04+).

## Decision

- Release Linux binaries are built in a **Ubuntu 22.04 container** (image pinned by digest) with LLVM 21 from
  apt.llvm.org and libstdc++ 13 (or newer) from the `ubuntu-toolchain-r/test` PPA, and Qt 6.8 LTS official binaries
  (glibc ≥ 2.28). Result: glibc requirement 2.35.
- **AppImage** bundles Qt and **libstdc++/libgcc_s** and uses them only when the host's copy is older (the
  "checkrt" approach: the AppRun compares `GLIBCXX` versions); it runs on Ubuntu 22.04+.
- **.deb** (package name `hardwario-monitor`) installs a private Qt into `/opt/hardwario-monitor/`
  (`bin/`, `lib/`, `plugins/`, `qml/`, `translations/`, `licenses/`) with `$ORIGIN`-relative RPATH, and symlinks
  `/usr/bin/hio-monitor` and `/usr/bin/hio`. Depends: `libc6 (>= 2.35)`, `libstdc++6 (>= 13)`, `bluez`, plus the X11/
  Wayland/GL/fontconfig libraries Qt needs (computed with `dpkg-shlibdeps` on the bundled libraries). It requires
  Ubuntu 24.04+ / Debian 13+.
- The .deb installs the J-Link udev rule of ADR-0072 as `/usr/lib/udev/rules.d/60-hardwario-jlink.rules` and reloads
  udev in `postinst`. The AppImage cannot; the doctor/setup wizard keeps offering the rule (ADR-0072).
- The .deb also installs the desktop file, AppStream metainfo and icons under `/usr/share`.
- Normal CI keeps building on `ubuntu-24.04` (ADR-0021); only the packaging jobs use the container.

## Consequences

- One Linux build per release feeds both formats.
- The .deb is self-contained and larger (~100 MB installed), but independent of distribution Qt.
- A conflicting rule from SEGGER's own .deb (`99-jlink.rules`) is harmless (same mode).
