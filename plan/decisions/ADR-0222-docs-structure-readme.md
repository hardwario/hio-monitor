# ADR-0222 — Documentation structure: a page per topic, a README product page, no screenshots in v2.0

- **Status:** Accepted (confirms the page list of ADR-0138, supersedes its generated `cli.md`)
- **Date:** 2026-11-03
- **Decided by:** Matej Strnad (plan 0029 session)

## Context

The repository has no README. tower-bridge uses a README product page plus a one-file manual; ADR-0138 listed a page
per topic and a `cli.md` generated from `hwio --help` with a CTest freshness check.

## Options considered

- Structure: tower-bridge model (one-file manual) / **ADR-0138's page per topic (chosen)**.
- README: **product page as tower-bridge (chosen)** / short README.
- Screenshots: from the mock kit, light + dark / light only / **none in v2.0 (chosen)**.
- `cli.md`: **hand-written, checked against `--help` (chosen)** / generated with a CTest.
- `troubleshooting.md`: **every `hwio::Error` code with an anchor, grouped by category (chosen)** / the common
  problems only.

## Decision

- `README.md`: what the app does, supported hardware, download table, first run per OS, command-line examples,
  links into `docs/`, building from source, repository layout, licence (MIT, ADR-0206). No images.
- `docs/<lang>/` pages per ADR-0221; `docs/README.md` is the language chooser.
- `docs/en/cli.md` is hand-written like tower-bridge's `cli.md` and checked once against `hwio … --help` of the
  integrated build; the generator and `docs_cli_fresh` CTest of plan 0017 phase E are dropped.
- `troubleshooting.md` has an anchor per error id (`probe.not_found` → `#probenot_found` on GitHub), with the
  message, the likely cause and the fix from the code, grouped by category / exit code.
- Developer documentation stays in `CLAUDE.md`, `packaging/README.md` and `packaging/RELEASE.md`, linked from the README.

## Consequences

- `cli.md` can drift from the code; the release checklist gets a "docs match `--help`" check.
- Screenshots can be added later without a structural change.
