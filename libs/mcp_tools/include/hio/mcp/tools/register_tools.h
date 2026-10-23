#pragma once

// Registration of the MCP device tools, resources and the debug_device prompt (plan/features/0013
// C1-C3, ADR-0099, ADR-0107). The CLI (`hio mcp`) and the GUI (plan 0014) call registerTools()
// once before they create the McpServer.
//
// Tools (phases A-C): list_devices, connect, disconnect, status, send_command, read_terminal,
// read_log, wait_for_state, wait_for_connection, reconnect, stop, start, release_probe,
// reacquire_probe, target_status, reset, halt, go, read_memory, read_registers, memory_zones,
// read_pib, read_uicr, write_memory, write_flash, flash, flash_lte, erase, recover. The phase D
// tools (ble_scan, diagnose, doctor) are not registered yet.
// Resources: hio://devices, hio://device/{id}/status, hio://device/{id}/log,
// hio://device/{id}/terminal. Prompt: debug_device.

#include "hio/core/result.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/tools/tools_context.h"

namespace hio::mcp::tools {

// `context` must outlive the registry and the server built from it. Errors: internal.unexpected
// (a registration failed, detail = the reason; a programming error).
[[nodiscard]] hio::Status registerTools(hio::mcp::Registry& registry, ToolsContext& context);

} // namespace hio::mcp::tools
