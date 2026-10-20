# ADR-0125 — Releases are built on version tags into a draft GitHub release

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M7 planning session)

## Context

The old app built only when a GitHub release was created by hand, with sed-patched versions. v2 takes its version
from git tags (`cmake/HioVersion.cmake`, plan 0005). ADR-0012: packages on every tag, unsigned on PRs.

## Options considered

1. **Tag push → build, sign, checksum, attest → draft release; a human publishes it (chosen).**
2. Manual `workflow_dispatch` with a version input.
3. Human creates the release, CI attaches assets (as before).

## Decision

- Workflow `.github/workflows/release.yml`, trigger: push of a tag matching `v2.*.*` (incl. pre-release tags such
  as `v2.0.0-rc.1`). Checkout with full history so the version is the tag (no override).
- **Gates** (the release job fails before signing if any fails): full test suite on all three OSes, `check_translations`
  with **0 unfinished `cs` entries** (ADR-0027), `HIO_TEST_OVERRIDES=OFF` (configure check), version computed from
  the tag equals the tag exactly (no `-dev`, no `.dirty`), the LICENSE sign-off item of ADR-0119 (checked by the
  human before tagging, listed in the release checklist).
- Jobs: Linux container build → AppImage + .deb; macOS universal → signed, notarized, stapled DMG; Windows → signed
  MSI. Then one job writes `SHA256SUMS`, creates **GitHub build provenance attestations**
  (`actions/attest-build-provenance`) for every artifact, uploads the symbol archives (ADR-0123) and creates a
  **draft** release with generated notes. A maintainer reviews and publishes it; pre-release tags are marked
  pre-release.
- Artifact names: `hio-monitor-<version>-x86_64.AppImage`, `hardwario-monitor_<version>_amd64.deb`,
  `hio-monitor-<version>-macos-universal.dmg`, `hio-monitor-<version>-windows-x64.msi`, `SHA256SUMS`,
  `hio-monitor-<version>-symbols-<os>.zip`.
- PR and `v2-dev` CI additionally build **unsigned** packages on one job per OS and upload them as workflow
  artifacts (7 days retention) so packaging breakage is caught before a release.
- Releases are published on `github.com/hardwario/hio-monitor` (same repository as v1; the update check filters
  `v2.*` tags, ADR-0116).

## Consequences

- A published release is always the exact output of CI for that tag.
- Tags are protected (repository rule: only maintainers may create `v2.*` tags).
