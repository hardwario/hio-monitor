# ADR-0120 — Code signing: Apple Developer ID with notarization, Windows via Azure Trusted Signing

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M7 planning session)

## Context

Open question 6. The old app signed and notarized on macOS with a .p12 and an Apple ID app-specific password; Windows
was never signed. Since 2023, Windows OV/EV certificates must live on hardware or a cloud HSM.

## Options considered

1. **Apple Developer ID + notarytool; Windows via Azure Trusted Signing (chosen).**
2. Apple + a purchased OV/EV certificate in a cloud HSM (DigiCert KeyLocker, SSL.com eSigner).
3. macOS only; Windows unsigned.

## Decision

- **macOS:** HARDWARIO's "Developer ID Application" certificate (.p12 imported into a temporary keychain in CI),
  hardened runtime with the entitlements of ADR-0126, `--timestamp`, notarization with `xcrun notarytool` using an
  **App Store Connect API key** (not an Apple ID password), then `xcrun stapler staple` on the DMG.
- **Windows:** **Azure Trusted Signing** (now also called Azure Artifact Signing), via the official GitHub action
  pinned by commit SHA, authenticated with GitHub OIDC federated credentials (no long-lived client secret). Signed:
  `hio-monitor.exe`, `hio.exe`, our own DLLs if any, and the MSI. Qt DLLs are already signed by The Qt Company and
  are not re-signed. Timestamps are always applied.
- **Linux:** no package signing in M7. Integrity comes from `SHA256SUMS` and GitHub build provenance attestations
  (ADR-0125).
- **Ownership:** the certificates, the Azure account and the App Store Connect key belong to HARDWARIO a.s.; the
  secrets live in a GitHub **environment `release`** with required reviewers, available only to the release
  workflow on tags. PR builds are never signed.
- Secret names and the rotation procedure are documented in `packaging/README.md` (0015).

## Consequences

- Azure Trusted Signing requires an organisation with a verifiable history in a supported country; if HARDWARIO is not
  accepted, a new ADR switches to option 2. 0015 lists this as a risk to check first.
- SmartScreen reputation still builds up over the first downloads.
