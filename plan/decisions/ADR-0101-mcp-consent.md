# ADR-0101 — Consent for MCP tools: GUI dialogs with per-client remembering, CLI opt-in flags

- **Status:** Accepted (amended by ADR-0108: the prompt is its own always-on-top window)
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (M6 planning session)

## Context

plan/0004-mcp.md requires user confirmation for destructive tools in the GUI and visible logging. The CLI has no
UI. MCP tool annotations (`destructiveHint`) are hints only and must not be relied on.

## Options considered

- GUI: **ask, with "remember for this client" (chosen)** / always ask / per-category settings only.
- CLI: **opt-in `--allow` flags (chosen)** / everything allowed / MCP elicitation with a fallback.

## Decision

- **Categories:** `flash` (`flash`, `flash_lte`, `write_flash` outside UICR), `erase` (`erase`, `recover`),
  `memory-write` (`write_memory`), `uicr` (any write touching UICR: `flash` with UICR records + `allow_uicr`,
  `write_flash` in UICR), and `connect` (GUI only: opening a new tab). A call may need several categories; all are
  checked. Control tools (`reset`, `halt`, `go`, `reconnect`, `stop`, `start`, `release_probe`,
  `reacquire_probe`), console and read tools never ask. They are always logged (ADR-0105).
- **GUI (0014):** each category has a setting, **Ask** (default for all) or **Always allow**. With Ask, a prompt (its own
  always-on-top window, ADR-0108) shows the client name (`clientInfo`), the action, the device and the details (file name, size,
  catalog id, target, address range). The buttons are **Allow once**, **Allow <category> for <client> until it
  disconnects**, and **Deny**. "Until it disconnects" means the legacy session's end, or for modern clients
  30 min without requests from that `clientInfo.name`. One prompt at a time, at most 8 queued. With no answer
  within 120 s the request is denied (`mcp.consent_timeout`). Deny gives `mcp.consent_denied`. A prompt never
  appears for a call the client has already cancelled.
- **CLI:** `hio mcp --allow flash,erase,memory-write,uicr` or `--allow all`. A tool whose category is not allowed
  stays listed (with `destructiveHint`), but every call returns `mcp.tool_not_allowed` naming the flag. `connect`
  is always allowed in the CLI.
- `libs/mcp` sees consent only through `IConsentBroker::request(ConsentRequest) → async ConsentDecision`.

## Consequences

- An AI in a flash → test loop asks once per client. CI scripts state their permissions in the client config.
- The UICR category protects factory data (PIB) even when flashing is allowed.
