#pragma once

// Registration of the MCP device tools, resources and the debug_device prompt (plan/features/0013
// C1-C3, ADR-0099, ADR-0107). The CLI (`hwio mcp`) and the GUI (plan 0014) call registerTools()
// once before they create the McpServer.
//
// Tools (phases A-C): list_devices, connect, disconnect, status, send_command, read_terminal,
// read_log, wait_for_state, wait_for_connection, reconnect, stop, start, release_probe,
// reacquire_probe, target_status, reset, halt, go, read_memory, read_registers, memory_zones,
// read_pib, read_uicr, write_memory, write_flash, flash, flash_lte, erase, recover. The phase D
// tools (ble_scan, diagnose, doctor) are not registered yet.
// Resources: hwio://devices, hwio://device/{id}/status, hwio://device/{id}/log,
// hwio://device/{id}/terminal. Prompt: debug_device.

#include "hwio/core/result.h"
#include "hwio/mcp/registry.h"
#include "hwio/mcp/tools/tools_context.h"

namespace hwio::mcp::tools {

// `context` must outlive the registry and the server built from it. Errors: internal.unexpected
// (a registration failed, detail = the reason; a programming error).
[[nodiscard]] hwio::Status registerTools(hwio::mcp::Registry& registry, ToolsContext& context);

} // namespace hwio::mcp::tools
