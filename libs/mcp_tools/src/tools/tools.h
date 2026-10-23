#pragma once

// Registration of the tool groups (plan/features/0013 C1-C2, ADR-0099); see register_tools.h.

#include "hio/core/result.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/tools/tools_context.h"

#include <QString>

namespace hio::mcp::tools::detail {

// list_devices, connect, disconnect, status
[[nodiscard]] hio::Status registerDiscoveryTools(hio::mcp::Registry& registry,
                                                 ToolsContext& context);
// send_command, read_terminal, read_log, wait_for_state, wait_for_connection
[[nodiscard]] hio::Status registerConsoleTools(hio::mcp::Registry& registry, ToolsContext& context);
// reconnect, stop, start, release_probe, reacquire_probe
[[nodiscard]] hio::Status registerSessionTools(hio::mcp::Registry& registry, ToolsContext& context);
// target_status, reset, halt, go, read_memory, read_registers, memory_zones, read_pib, read_uicr,
// write_memory, write_flash
[[nodiscard]] hio::Status registerTargetTools(hio::mcp::Registry& registry, ToolsContext& context);
// flash, flash_lte, erase, recover
[[nodiscard]] hio::Status registerFirmwareTools(hio::mcp::Registry& registry,
                                                ToolsContext& context);
// debug_device
[[nodiscard]] hio::Status registerPrompts(hio::mcp::Registry& registry);
[[nodiscard]] QString debugDevicePromptText();

// Annotation presets.
inline constexpr hio::mcp::ToolAnnotations readOnly{
    .readOnlyHint = true, .destructiveHint = false, .idempotentHint = true, .openWorldHint = false};
inline constexpr hio::mcp::ToolAnnotations control{.readOnlyHint = false,
                                                   .destructiveHint = false,
                                                   .idempotentHint = false,
                                                   .openWorldHint = false};
inline constexpr hio::mcp::ToolAnnotations destructive{.readOnlyHint = false,
                                                       .destructiveHint = true,
                                                       .idempotentHint = false,
                                                       .openWorldHint = false};

} // namespace hio::mcp::tools::detail
