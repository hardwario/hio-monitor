#pragma once

// hio::mcp::Limits - every bound and timeout of the MCP server (plan/features/0013 "Bounds",
// ADR-0097, ADR-0102, ADR-0104, ADR-0107). The defaults are the plan's values; tests shrink them
// instead of using a fake clock (there is no owner-thread clock abstraction).
//
// The server and transports use the protocol part; `tools` holds the bounds of the tool handlers
// (hio_mcp_tools reads them through ToolEnv::limits()).

#include <chrono>
#include <cstddef>

namespace hio::mcp {

struct ToolLimits {
    // send_command (ADR-0104)
    std::chrono::milliseconds sendCommandMinTimeout = std::chrono::milliseconds{100};
    std::chrono::milliseconds sendCommandMaxTimeout = std::chrono::seconds{120};
    std::chrono::milliseconds sendCommandSilence = std::chrono::milliseconds{300};
    std::size_t commandMaxBytes = std::size_t{4} * 1024;
    std::size_t sendCommandMaxLines = 2000;
    std::size_t sendCommandMaxBytes = std::size_t{256} * 1024;
    // read_log / read_terminal
    std::size_t readMaxLines = 5000;
    std::size_t readMaxBytes = std::size_t{1024} * 1024;
    // read_log with a pattern (review R10): at most this many lines are scanned per call (the
    // regex runs synchronously on the owner thread); more -> truncated with a continuation cursor.
    std::size_t readScanMaxLines = 20000;
    // Regex matching (read_log, wait_for_state; review R10) looks at the first this many bytes of
    // a line (UTF-8).
    std::size_t regexMaxLineBytes = std::size_t{4} * 1024;
    // wait_for_state / wait_for_connection
    std::chrono::milliseconds waitMaxTimeout = std::chrono::seconds{600};
    std::size_t regexMaxChars = 512;
    // target and firmware tools
    std::size_t readMemoryInlineBytes = std::size_t{64} * 1024;
    std::size_t readMemoryFileBytes = std::size_t{16} * 1024 * 1024;
    std::size_t writeMemoryMaxBytes = std::size_t{64} * 1024;
    std::size_t writeFlashMaxBytes = std::size_t{1024} * 1024;
    std::size_t imageFileMaxBytes = std::size_t{64} * 1024 * 1024;
    std::chrono::milliseconds operationStall = std::chrono::seconds{60};
    std::size_t maxRegisters = 128;
    std::size_t maxZones = 32;
};

struct Limits {
    // Tool calls (ADR-0097): more -> tool result mcp.too_many_requests.
    std::size_t maxInFlightCalls = 32;
    // Progress notifications per call (ADR-0107): at most one per interval (4/s).
    std::chrono::milliseconds progressInterval = std::chrono::milliseconds{250};
    // Blocking handler work on worker threads (ADR-0162, Offloader): at most this many jobs run
    // at once, more wait in a FIFO; beyond that -> mcp.too_many_requests.
    std::size_t maxOffloadRunning = 4;
    std::size_t maxOffloadQueued = 16;

    // HTTP (ADR-0102)
    std::size_t maxHttpConnections = 16;
    std::size_t maxHeaderBytes = std::size_t{16} * 1024; // request line + headers
    std::size_t maxHeaderCount = 100;                    // header lines per request
    std::size_t maxBodyBytes = std::size_t{4} * 1024 * 1024;
    std::chrono::milliseconds headerTimeout = std::chrono::seconds{10};
    std::chrono::milliseconds bodyTimeout = std::chrono::seconds{30};
    std::chrono::milliseconds idleTimeout = std::chrono::seconds{60};
    // A refused connection (error response + Connection: close) is aborted when the response has
    // not been flushed within this time (a client that does not read cannot keep its slot).
    std::chrono::milliseconds closingTimeout = std::chrono::seconds{5};
    std::size_t maxSendQueueBytes =
        std::size_t{1024} * 1024; // per connection; more -> slow client dropped
    // JSON if the call is done within this window, SSE otherwise.
    std::chrono::milliseconds jsonResponseWindow = std::chrono::milliseconds{250};
    std::chrono::milliseconds sseHeartbeat = std::chrono::seconds{15};
    std::size_t authFailureLimit = 10;
    std::chrono::milliseconds authFailureWindow = std::chrono::seconds{60};
    std::chrono::milliseconds authLockout = std::chrono::seconds{10};

    // Sessions and subscriptions (ADR-0095, ADR-0101, ADR-0107)
    std::size_t maxLegacySessions = 16;
    std::chrono::milliseconds legacySessionIdle = std::chrono::minutes{30};
    std::size_t maxModernClients = 64; // remembered clientInfo names (consent clientGone)
    std::chrono::milliseconds modernClientIdle = std::chrono::minutes{30};
    std::size_t maxListenStreams = 16; // subscriptions/listen + legacy GET streams
    std::size_t maxSubscriptions = 64; // resource URIs over all sessions and listen streams
    std::chrono::milliseconds resourceUpdateInterval = std::chrono::seconds{1};

    // stdio (ADR-0102, ADR-0154)
    std::size_t maxStdioLineBytes = std::size_t{4} * 1024 * 1024;
    std::size_t stdinQueueLines = 64;
    std::size_t stdioLinesPerWakeup = 16; // lines handled per owner-thread turn

    // Activity entries (ADR-0105): the argument summary is cut to this many characters.
    std::size_t activityArgumentChars = 200;

    ToolLimits tools;
};

} // namespace hio::mcp
