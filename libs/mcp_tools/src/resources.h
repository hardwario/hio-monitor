#pragma once

// MCP resources of the device tools (ADR-0107, plan/features/0013 C3): hwio://devices (JSON list),
// hwio://device/{id}/status (JSON), hwio://device/{id}/log and hwio://device/{id}/terminal (text,
// the last resourceLines lines). The device id goes into the URI as it is ("jlink:801000001").
//
// Thread safety: owner thread only.

#include "hwio/core/result.h"
#include "hwio/mcp/registry.h"
#include "hwio/mcp/tools/tools_context.h"

#include <QString>

#include <cstddef>
#include <string_view>

namespace hwio::mcp::tools::detail {

inline constexpr std::size_t resourceLines = 1000;

[[nodiscard]] QString devicesUri();
// "hwio://device/<id>/<part>" (part: "status", "log", "terminal")
[[nodiscard]] QString deviceUri(std::string_view id, const QString& part);

[[nodiscard]] hwio::Status registerResources(hwio::mcp::Registry& registry, ToolsContext& context);

} // namespace hwio::mcp::tools::detail
