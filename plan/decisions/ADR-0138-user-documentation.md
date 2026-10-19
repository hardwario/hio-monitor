# ADR-0138 — User documentation lives in docs/ in the repository; docs.hardwario.com publishing decided later

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M8 planning session)

## Context

v2.0 needs install, first-steps, CLI, MCP and troubleshooting documentation. The user wants both in-repository docs and
docs.hardwario.com; how content gets to docs.hardwario.com will be decided by the user later.

## Options considered

1. In-repo `docs/` only.
2. docs.hardwario.com only.
3. **Both: `docs/` is the source of truth now; publishing to docs.hardwario.com is decided later (chosen).**

## Decision

- `docs/` (Markdown, English) in the repository:
  `install.md` (per OS, J-Link Software, udev, macOS permissions), `getting-started.md`, `console.md`, `flashing.md`,
  `bluetooth.md`, `mcp.md` (Claude Code / Claude Desktop / Cursor / VS Code setup), `cli.md` (generated from
  `hio --help` output by a CTest-checked script), `cli-migration.md` (from `hardwario`, ADR-0139), `troubleshooting.md`
  (index by `hio::Error` code), `privacy.md` (update check, diagnostic report, nothing uploaded).
- Language of `docs/`: English only in v2.0 (the app UI is translated, ADR-0027); Czech docs may follow later.
- In-app links (About, wizards, error panels) point to the GitHub rendering of `docs/…` on `main` via one URL table in
  `libs/core` (`docsUrl(topic)`), so the target can switch to docs.hardwario.com by changing one table.
- Publishing to docs.hardwario.com: new open question 29, owner the user.

## Consequences

- Docs are reviewed in the same PRs as the features they describe.
- Open question 20 (documentation links per error code) is resolved by `troubleshooting.md` anchors + `docsUrl`.
