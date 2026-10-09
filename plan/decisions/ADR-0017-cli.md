# ADR-0017 — Command-line interface

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad

## Context

The backend is UI-independent (ADR-0004), so a full CLI is cheap. Developers and CI currently
use py-hardwario's `hardwario` command. Previously the CLI was only planned as the MCP stdio host (M6).

## Decision

- The CLI is a first-class deliverable (`app/cli`), described in `plan/0006-cli.md`.
- **Command tree and options are compatible with py-hardwario** (`chester app ...`,
  `chester lte ...`, later `device <family> ...`).
- **It grows with each milestone**: CLI skeleton in M0; every backend feature plan includes its
  CLI commands and tests.
- Argument parsing with **CLI11**, fetched via CPM pinned to tag + hash (ADR-0010).
- **Human-readable output by default, `--json` for machine-readable output**; errors rendered
  from `hio::Error`; stable, documented exit codes.

## Consequences

- New dependency: CLI11 (BSD-3, header-only).
- Feature plans must have a CLI section; the template is updated.
- The CLI serves as the headless hardware test harness before the GUI exists.
- Exit codes and `--json` schemas are a public contract and need versioning care.
