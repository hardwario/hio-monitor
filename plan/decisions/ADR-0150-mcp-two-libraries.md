# ADR-0150 — MCP code in two libraries: `hio_mcp` (protocol) and `hio_mcp_tools` (tools)

- **Status:** Accepted
- **Date:** 2026-10-20
- **Decided by:** Matej Strnad (M6 implementation session, sync of plan 0013 with the code)

## Context

Plan 0013 said `libs/mcp` uses QtCore + QtNetwork only. The tool handlers, however, need `hio::device` (J-Link,
nRF Util, Qt Bluetooth through `hw/ble`) and later `hio::diag`. One library would pull all of that into the protocol
core, its unit tests and its fuzz targets.

## Options considered

1. **Two libraries** — protocol core without device dependencies, tools on top.
2. One library that links everything.

## Decision

Option 1.
- `libs/mcp` → static `hio_mcp` (`hio::mcp`, namespace `hio::mcp`): JSON-RPC, both dialects, registry, `ToolCall`,
  schema validation, stdio and HTTP transports, `TokenStore`, `IConsentBroker`, `IActivitySink`. Links QtCore,
  QtNetwork and `hio::core` only. A CMake check fails the configure step if it links anything else from `hio::`.
- `libs/mcp_tools` → static `hio_mcp_tools` (`hio::mcp_tools`, namespace `hio::mcp::tools`): `IDeviceProvider`,
  `DeviceEntry`/`DeviceRef`, all tool handlers, resources and the `debug_device` prompt. Links `hio_mcp`,
  `hio::device`, `hio::firmware` (and `hio::diag` from phase D).
- Both are created with `hio_add_backend_library` (ADR-0022, ADR-0004 GUI-module denylist applies).

## Consequences

- The protocol core and the fuzzers build and test without any hardware code.
- One more target and CMake list; the GUI (0014) links both.
