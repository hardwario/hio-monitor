# ADR-0116 — Update check notifies only; no in-app updater

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M7 planning session)

## Context

Open question 2. The old app compared `tag_name` from GitHub `releases/latest` with `!=` and showed a modal dialog at
every start (0002-legacy-inventory.md). In-app updaters (Sparkle, WinSparkle, AppImageUpdate) download and run code,
need their own signing keys and differ per package format; .deb users update through their package manager anyway.

## Options considered

1. **Notify only (chosen):** GitHub Releases API, semantic-version comparison, non-modal banner, link to the
   release page.
2. In-app updater per platform.
3. No update check.

## Decision

- The app asks the GitHub Releases API of `hardwario/hio-monitor` for releases whose tag matches `v2.*`, ignores
  drafts, and ignores pre-releases unless the user opted in. Versions are compared by semantic versioning
  (SemVer 2.0.0 precedence, build metadata ignored).
- A newer version is shown as a **non-modal banner** with [Download] (opens the release page in the browser),
  [Skip this version] and [Later]. Nothing is downloaded or installed by the app.
- The check runs at most once per 24 h, in the background, and can be turned off in Settings. A manual
  "Check for updates" exists in Settings › About and in the CLI.
- Failures (offline, rate limit, bad response) are silent in the automatic check and reported as `hio::Error` in
  the manual one.
- Details (interval, setting keys, CLI command, banner texts) are in 0016.

## Consequences

- No update-signing infrastructure, no self-replacing binaries, no per-format updater.
- Users install updates themselves; the .deb is updated by the user installing the new .deb (no APT repository in
  M7).
- The legacy defects (string `!=`, modal dialog at every start, `Qt.quit()` after Download) are not repeated.
