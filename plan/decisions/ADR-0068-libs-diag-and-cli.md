# ADR-0068 — Backend module `libs/diag` with CLI `doctor`, `diag report`, `chester … check`

- **Status:** Accepted
- **Date:** 2026-10-09
- **Decided by:** Matej Strnad (M4 planning session)

## Context

The wizards need environment checks (J-Link Software, nRF Util, USB access, running programs that may hold the
probe, probes, writable directories) and a diagnostic report. CLAUDE.md requires that the GUI contains no logic and
that every backend feature has a CLI command.

## Options considered

1. **A new module `libs/diag` and CLI commands (chosen).**
2. Check functions spread over `libs/hw`, `libs/device`, `libs/core`, aggregated in the GUI.
3. `libs/diag` without a CLI.

## Decision

- `libs/diag` (QtCore only, ADR-0004; static internal library, ADR-0022) contains:
  - `Check` / `CheckResult` (`id`, `status` Ok / Warning / Failed / Skipped, title, short result text,
    `std::optional<hio::Error>`) and `CheckRunner` (runs a list of checks on a caller-owned thread, reports each
    result, honours a stop token, per-check timeout);
  - the environment checks (J-Link Software, nRF Util, Linux USB access, probe holders, probes, probe firmware,
    settings / log directories);
  - the process scan (ADR-0073), the USB access check (ADR-0072) and the report builder (ADR-0075).
- It reuses the M1 `LibraryLocator` / `ProbeEnumerator`, the M2 `NrfUtilLocator`, `Logging` and `Settings`; it adds no
  new vendor calls (`libs/hw/*` stays the only place that touches vendor APIs).
- CLI (no py-hardwario equivalent, so no compatibility constraint):
  - `hio doctor [--json]` — the environment checks;
  - `hio diag report [-o FILE] [--json]` — writes the diagnostic zip;
  - `hio chester app|lte check [-n SN] [--json]` — the live connection check (ADR-0069).

## Consequences

- One implementation of each check for the GUI, the CLI and later MCP (M6).
- Support can ask users to run `hio doctor` and attach `hio diag report`.
