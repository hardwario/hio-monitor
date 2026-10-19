# ADR-0126 — macOS hardened runtime with the disable-library-validation entitlement

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M7 planning session)

## Context

Notarization requires the hardened runtime. The hardened runtime's library validation only loads libraries signed
by Apple or by the app's own team. The JLinkARM dylib (ADR-0006) and its per-session copies (ADR-0030) are signed by
SEGGER, so they would be rejected.

## Options considered

1. **Entitlement `com.apple.security.cs.disable-library-validation` (chosen).**
2. Re-sign the copies with our identity at runtime: impossible without the private key on the user's machine.

## Decision

- `HARDWARIO Monitor.app` and the embedded `hio` are signed with hardened runtime and the entitlements file
  `packaging/macos/entitlements.plist` containing `com.apple.security.cs.disable-library-validation` = true. No
  other exceptions (no JIT, no unsigned executable memory, no DYLD environment variables).
- The Bluetooth usage descriptions of ADR-0086 stay in both Info.plists; no Bluetooth entitlement is needed outside
  the App Sandbox (the app is not sandboxed).
- A CTest on macOS checks the entitlements of both binaries (`codesign -d --entitlements`), and the macOS manual
  check loads J-Link from `/Applications/SEGGER/JLink` in a notarized build.

## Consequences

- Any dylib could be loaded into the process; acceptable because the app only loads the J-Link library from the
  path found by ADR-0006 discovery or set by the user.
