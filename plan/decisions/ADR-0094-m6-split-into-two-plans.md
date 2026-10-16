# ADR-0094 — M6 is split into a core + CLI plan and a GUI plan

- **Status:** Accepted
- **Date:** 2026-10-16
- **Decided by:** Matej Strnad (M6 planning session)

## Context

M6 adds the integrated MCP server (ADR-0008, plan/0004-mcp.md). It needs a protocol core, two transports
(stdio, Streamable HTTP), about 30 tools over `libs/device` and `libs/diag`, new low-level target operations in
`libs/device`, a CLI command, and the GUI integration: server lifecycle, token, consent dialogs and activity
display. Earlier milestones were split so that each plan stays reviewable and can be built by one agent
(ADR-0059, ADR-0065, ADR-0078).

## Options considered

1. **Two plans: core + tools + both transports + CLI, then GUI (chosen).**
2. Three plans: core + stdio + CLI; HTTP transport; GUI.
3. One phased plan.

## Decision

| Plan | Content | Depends on |
|---|---|---|
| **0013 — MCP core, tools and CLI** | `libs/mcp` (JSON-RPC, both protocol revisions, tool/resource/prompt registry, stdio + HTTP transports, `IDeviceProvider` and `IConsentBroker` seams), all tools, new `DeviceSession` low-level operations, profile additions, `hio mcp [--http]` | M1, M2 (0002, 0003); M4 0007 phase A (`libs/diag`) for the diagnosis tools; M5 0011 for BLE devices |
| **0014 — MCP in the GUI** | GUI device provider (tabs), consent dialogs and rules, MCP settings section, token file UI, client-config snippets, console markers, activity list, header indicator, AI busy state on tabs | 0013, M3 0004/0005/0006 |

## Consequences

- The whole server is testable headless from the CLI (`hio mcp --http` covers the HTTP transport, ADR-0106).
- 0014 contains no protocol code; it only implements the two seams of `libs/mcp` and the UI.
