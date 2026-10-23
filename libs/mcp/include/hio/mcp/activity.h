#pragma once

// Activity seam of the MCP server (ADR-0105, plan/features/0013 "Device and consent seams").
//
// McpServer records one ActivityEntry for every tools/call when it ends: success, tool error,
// protocol error (unknown tool, invalid arguments) or cancellation. The sink bounds what it keeps
// (GUI: the last 1000 entries; CLI: the hio.mcp log). Console markers are written by the tool
// handlers (hio_mcp_tools), which know the device; ToolSpec::marker is copied here for the sink.
//
// Thread safety: owner thread only.

#include "hio/mcp/request_context.h"

#include <QDateTime>

#include <chrono>
#include <string>

namespace hio::mcp {

struct ActivityEntry {
    QDateTime time; // UTC, when the call was received
    Dialect dialect = Dialect::Modern;
    std::string client;    // clientInfo.name
    std::string clientKey; // RequestContext::clientKey
    std::string tool;      // as requested (also for unknown tools)
    // ToolCall::deviceId() when the handler reported one, else the "device" argument, else empty.
    std::string deviceId;
    // Compact JSON of the arguments, string values over 64 characters shortened, cut to
    // Limits::activityArgumentChars characters (with "…").
    std::string arguments;
    // "ok", an hio::Error id ("mcp.consent_denied", "cancelled", "mcp.too_many_requests", ...) or
    // "rpc.invalid_params" for a JSON-RPC error (unknown tool, invalid arguments).
    std::string result;
    std::chrono::milliseconds duration{0};
    bool marker = false; // ToolSpec::marker
};

class IActivitySink {
public:
    IActivitySink() = default;
    IActivitySink(const IActivitySink&) = delete;
    IActivitySink& operator=(const IActivitySink&) = delete;
    IActivitySink(IActivitySink&&) = delete;
    IActivitySink& operator=(IActivitySink&&) = delete;
    virtual ~IActivitySink() = default;

    virtual void record(ActivityEntry entry) = 0;
};

} // namespace hio::mcp
