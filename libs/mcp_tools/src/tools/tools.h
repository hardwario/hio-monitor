#pragma once

// Registration of the tool groups (plan/features/0013 C1-C2, ADR-0099); see register_tools.h.

#include "hwio/core/result.h"
#include "hwio/mcp/registry.h"
#include "hwio/mcp/tools/tools_context.h"

#include <QString>

namespace hwio::mcp::tools::detail {

// list_devices, connect, disconnect, status
[[nodiscard]] hwio::Status registerDiscoveryTools(hwio::mcp::Registry& registry,
                                                  ToolsContext& context);
// send_command, read_terminal, read_log, wait_for_state, wait_for_connection
[[nodiscard]] hwio::Status registerConsoleTools(hwio::mcp::Registry& registry,
                                                ToolsContext& context);
// reconnect, stop, start, release_probe, reacquire_probe
[[nodiscard]] hwio::Status registerSessionTools(hwio::mcp::Registry& registry,
                                                ToolsContext& context);
// target_status, reset, halt, go, read_memory, read_registers, memory_zones, read_pib, read_uicr,
// write_memory, write_flash
[[nodiscard]] hwio::Status registerTargetTools(hwio::mcp::Registry& registry,
                                               ToolsContext& context);
// flash, flash_lte, erase, recover
[[nodiscard]] hwio::Status registerFirmwareTools(hwio::mcp::Registry& registry,
                                                 ToolsContext& context);
// debug_device
[[nodiscard]] hwio::Status registerPrompts(hwio::mcp::Registry& registry);
[[nodiscard]] QString debugDevicePromptText();

// Annotation presets.
inline constexpr hwio::mcp::ToolAnnotations readOnly{
    .readOnlyHint = true, .destructiveHint = false, .idempotentHint = true, .openWorldHint = false};
inline constexpr hwio::mcp::ToolAnnotations control{.readOnlyHint = false,
                                                    .destructiveHint = false,
                                                    .idempotentHint = false,
                                                    .openWorldHint = false};
inline constexpr hwio::mcp::ToolAnnotations destructive{.readOnlyHint = false,
                                                        .destructiveHint = true,
                                                        .idempotentHint = false,
                                                        .openWorldHint = false};

} // namespace hwio::mcp::tools::detail
