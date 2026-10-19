# ADR-0140 — Hardware-in-the-loop CI on an opt-in self-hosted runner

- **Status:** Accepted
- **Date:** 2026-10-19
- **Decided by:** Matej Strnad (M8 planning session)

## Context

0005 planned a HIL runner "later". So far HIL is manual (checklists per plan). Releases should not depend on
someone remembering the manual runs.

## Options considered

1. **Self-hosted Linux runner, opt-in workflow, release gate (chosen).**
2. Keep HIL manual.
3. HIL on every PR.

## Decision

- One self-hosted runner at HARDWARIO (Linux x86_64, labels `self-hosted, hil, chester`) with a CHESTER (app + LTE
  connectors) on a J-Link and a USB-switchable power supply (optional; tests that need power cycling are skipped when
  absent). J-Link Software and nRF Util installed on the runner (never in the repo).
- Workflow `.github/workflows/hil.yml`: triggers `workflow_dispatch` and `push` of `v2.*` tags (then it is part of the
  release gate, ADR-0125/0135). **Never** on `pull_request` (fork PRs must not reach the hardware); the runner is
  restricted to the repository and the `hil` environment with required reviewers.
- Suite (CLI end-to-end against hardware): `jlink list/info`, `chester app flash` (local hex from a fixture artifact
  and a catalog id), `erase`, `reset`, `console` attach + `command` round trip, `lte flash` of the app part (modem
  firmware only on dispatch with an input flag, it takes minutes), `recover` on dispatch only, MCP stdio smoke test
  (`list_devices`, `send_command`), probe firmware status via `jlink info` (no update in CI).
- Runtime bound: ≤ 20 minutes for the default suite; each test has its own timeout.
- Results are uploaded as artifacts (logs, diagnostic report).

## Consequences

- Someone at HARDWARIO maintains the runner and the device (documented in `packaging/README.md` → `hil/README.md`).
- GUI HIL stays manual.
