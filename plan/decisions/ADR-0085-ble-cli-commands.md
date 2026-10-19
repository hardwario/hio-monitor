# ADR-0085 — BLE CLI commands: `ble scan`, `ble console`, `ble command`, `ble pair`

- **Status:** Accepted
- **Date:** 2026-10-10
- **Decided by:** Matej Strnad (M5 planning session)

## Context

0006-cli lists `ble scan` and `ble console` for M5; py-hardwario has no BLE, so there is no compatibility constraint.
Every backend feature gets a CLI command (CLAUDE.md), and scripts and MCP-like automation need a one-shot command.

## Options considered

1. **scan, console, command, pair (chosen).**
2. scan + console only.
3. scan, console, command.

## Decision

| Command | Summary |
|---|---|
| `hio ble scan [--timeout S=5] [--all] [--profile ID=chester]` | One scan; table or `--json` list of `{id, name, rssi, nusAdvertised, profile, inUseBy}`. No device → empty list, exit 0. |
| `hio ble console (-a/--address ID \| --name N) [--auto-reconnect] [--history-file P] [--console-file P] [--profile ID]` | Interactive console with the M1 replxx UI (ADR-0033) and NDJSON mode. `--name` scans (5 s) and needs exactly one match. |
| `hio ble command CMD (-a ID \| --name N) [-t/--timeout S=3.0] [--console-file P]` | Connect, send each line, collect shell output until the prompt is seen or the timeout (same semantics as the J-Link `command`: from the last echo, `"truncated"` in JSON — plan 0011 revision 2026-10-19), disconnect. `--json` = `chester app command` shape. |
| `hio ble pair (-a ID \| --name N)` | App-initiated pairing (ADR-0084); macOS → `ble.pairing_not_supported`. |

Global `--json` and exit codes per ADR-0024/0026. `--name` with several matches → `usage.invalid_argument` listing
the ids.

## Consequences

- `ble command`'s default timeout is 3 s (BLE connect + bonding is slower than RTT); the timeout counts from
  sending, as in M1.
- 0006-cli's command table is updated.
