# ADR-0117 — Windows installer is a per-machine MSI built with WiX

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M7 planning session)

## Context

Open question 8. The old app used the Qt Installer Framework (admin install, own maintenance tool, bundled J-Link
driver). v2 bundles no vendor software (ADR-0006).

## Options considered

1. **MSI via WiX (chosen):** standard Windows Installer, silent install (`msiexec /qn`), upgrade codes,
   Group Policy / Intune deployment, CPack has a WiX generator.
2. Inno Setup .exe installer.
3. Qt Installer Framework (as before).
4. MSIX: sandboxed; awkward with the per-session J-Link DLL copies (ADR-0030) and named pipes (ADR-0058).

Install scope: per-machine, per-user, or dual-purpose.

## Decision

- **MSI built with WiX** (WiX 4 or newer, pinned version; the exact toolset and whether CPack's WiX generator or an
  own `.wxs` is used are details of 0015).
- **Per-machine** install into `%ProgramFiles%\HARDWARIO\Monitor`, requires elevation, for all users.
- Fixed `UpgradeCode`; every release is a major upgrade that removes the previous v2 version. Downgrades are
  blocked with a message.
- Start-menu shortcut "HARDWARIO Monitor" with AppUserModelID `com.hardwario.monitor` (ADR-0018); desktop shortcut
  is an optional feature, off by default.
- The install directory is added to the **system PATH** (feature, on by default) so that `hio` works in new
  terminals (ADR-0122).
- The MSI does not contain or run the SEGGER J-Link installer or USB driver; the first-run setup wizard points to it
  (ADR-0006, 0008).
- The old app (Qt Installer Framework, `HARDWARIO Monitor` in Program Files) is not touched; both can be installed
  side by side until M8.

## Consequences

- Admin rights are needed to install. Users without them cannot install v2 on Windows (as with the old app).
- The MSI and every shipped `.exe`/`.dll` of ours are signed (ADR-0120).
