# ADR-0106 — CLI `hio mcp`: stdio by default, opt-in `--http`

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (M6 planning session)

## Context

ADR-0008 puts stdio in the CLI. Claude Desktop supports only stdio servers. Testing the HTTP transport headless and
running it in CI or over remote desktop needs HTTP without the GUI.

## Decision

`hio mcp [--http] [--port N=47800] [--token-file P] [--allow LIST] [--jlink-speed K] [--auto-reconnect]
[--console-file-dir P]`:

- **Default: stdio.** stdout carries only JSON-RPC. Logs and human messages go to stderr (warnings only) and the
  log file. The `--json` envelope does not apply (`--json` is a usage error for `mcp`).
- **`--http`:** the HTTP transport, as in the GUI (ADR-0102). The token comes from `--token-file`, or from
  `HIO_MCP_TOKEN`, or is generated for this run and printed once to stderr together with the URL. The run ends on
  Ctrl+C.
- Exit codes (ADR-0024): 0 on stdin EOF or client-requested shutdown, 130 on Ctrl+C / SIGTERM (as every command); `mcp.port_in_use` → Io (3); usage errors → 2.
- Sessions opened by the server are closed on exit. Probe locks (ADR-0058) make the GUI and `hio mcp` exclusive
  per probe, with the usual `probe.in_use_by_other_process` error.
- Client snippets (also shown by `hio mcp --help` and by 0014):
  - Claude Code stdio: `claude mcp add --transport stdio hio -- hio mcp --allow flash`
  - Claude Code HTTP: `claude mcp add --transport http hio http://127.0.0.1:47800/mcp --header "Authorization: Bearer <token>"`
  - Claude Code `.mcp.json` (token kept out of the file): `{"mcpServers":{"hio":{"type":"http","url":"http://127.0.0.1:47800/mcp","headers":{"Authorization":"Bearer ${HIO_MCP_TOKEN}"}}}}`
  - Claude Desktop: `{"mcpServers":{"hio":{"command":"<path>/hio","args":["mcp"]}}}`

## Consequences

- `hio mcp` is the only CLI command that runs `QCoreApplication::exec()` (ADR-0097).
- CLI end-to-end tests cover both transports with the fake J-Link scenarios.
