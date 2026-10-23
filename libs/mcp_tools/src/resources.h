#pragma once

// MCP resources of the device tools (ADR-0107, plan/features/0013 C3): hio://devices (JSON list),
// hio://device/{id}/status (JSON), hio://device/{id}/log and hio://device/{id}/terminal (text,
// the last resourceLines lines). The device id goes into the URI as it is ("jlink:801000001").
//
// Thread safety: owner thread only.

#include "hio/core/result.h"
#include "hio/mcp/registry.h"
#include "hio/mcp/tools/tools_context.h"

#include <QString>

#include <cstddef>
#include <string_view>

namespace hio::mcp::tools::detail {

inline constexpr std::size_t resourceLines = 1000;

[[nodiscard]] QString devicesUri();
// "hio://device/<id>/<part>" (part: "status", "log", "terminal")
[[nodiscard]] QString deviceUri(std::string_view id, const QString& part);

[[nodiscard]] hio::Status registerResources(hio::mcp::Registry& registry, ToolsContext& context);

} // namespace hio::mcp::tools::detail
