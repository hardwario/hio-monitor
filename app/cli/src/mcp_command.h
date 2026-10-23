#pragma once

// `hio mcp [--http]` - the MCP server of the CLI (plan/features/0013 "CLI" and "stdio transport and
// hio mcp", ADR-0097, ADR-0101, ADR-0102, ADR-0106, ADR-0154, ADR-0156).
//
// stdio (default): JSON-RPC lines on stdin/stdout; stdout carries nothing else. --http: the HTTP
// transport on 127.0.0.1:--port with a bearer token (HIO_MCP_TOKEN, else --token-file, else one
// generated for this run). The command runs QCoreApplication::exec() (ADR-0097; the only CLI
// command that does) until stdin EOF (stdio; exit 0; in-flight calls are cancelled, ADR-0097),
// a failed write to stdout (the client closed its end: outputClosedError, `cancelled`, exit 130,
// review S4) or Ctrl+C / SIGTERM (`cancelled`, exit 130). It returns streamed=true, so main()
// prints no envelope.

#include "cli_app.h"
#include "hio/core/result.h"
#include "hio/core/settings.h"
#include "hio/mcp/activity.h"

#include <CLI/App.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hio::cli {

// The default --port (ADR-0102) and its range.
inline constexpr std::uint16_t defaultMcpPort = 47800;
inline constexpr std::uint16_t minMcpPort = 1024;
inline constexpr std::uint16_t maxMcpPort = 65535;

// The activity sink of `hio mcp`: one hio.mcp log line per tools/call (ADR-0105; bounded by the
// log file rotation).
class LogActivitySink final : public hio::mcp::IActivitySink {
public:
    void record(hio::mcp::ActivityEntry entry) override;
};

// ADR-0163: the settings key of the stdin queue's byte budget in MiB (default 16, allowed 4-256).
inline constexpr std::string_view stdioBufferKey = "mcp/stdioBufferMiB";
inline constexpr int defaultStdioBufferMiB = 16;
inline constexpr int minStdioBufferMiB = 4;
inline constexpr int maxStdioBufferMiB = 256;

struct StdioBufferSetting {
    int mebibytes = defaultStdioBufferMiB; // always within the allowed range
    std::optional<std::string> warning;    // set when the stored value was invalid or clamped
};
// Reads mcp/stdioBufferMiB (`settings` may be nullptr: the default). An invalid value gives the
// default, an out-of-range one is clamped; both with a warning text.
[[nodiscard]] StdioBufferSetting stdioBufferSetting(const Settings* settings);

void registerMcpCommand(CLI::App& app, CommandSlot& slot, const GlobalOptions& globals);

} // namespace hio::cli
