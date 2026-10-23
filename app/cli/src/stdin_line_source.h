#pragma once

// StdinLineSource - the ILineSource of `hio mcp` over the CLI's StdinLineReader (plan/features/0013
// "stdio transport and hio mcp", ADR-0097, ADR-0154).
//
// The reader thread queues complete lines (at most `maxQueuedLines` and `maxQueuedBytes` of text,
// then it waits: backpressure)
// and calls the wake-up after each queued item. Lines longer than `maxLineBytes` are dropped
// (DropAndReport) and delivered as Line{overlong = true}; StdioTransport answers them with a
// JSON-RPC parse error. The end of input is tracked here: atEnd() is true once the reader's End
// item was taken.
//
// Thread safety: takeLine(), atEnd() and setWakeup() on the owner thread. The wake-up function is
// guarded by a mutex and called with it held, so once setWakeup() returns the previous function is
// never called again (the ILineSource contract).

#include "hio/mcp/stdio_transport.h"
#include "terminal.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace hio::cli {

class StdinLineSource final : public hio::mcp::ILineSource {
public:
    // Reads stdin, or `read` (tests). `maxLineBytes` and `maxQueuedLines` >= 1 (hio::mcp::Limits
    // maxStdioLineBytes = 4 MiB and stdinQueueLines = 64 in `hio mcp`); `maxQueuedBytes` is the
    // queue's byte budget (ADR-0163: the setting mcp/stdioBufferMiB in `hio mcp`; 0 = none).
    StdinLineSource(std::size_t maxLineBytes, std::size_t maxQueuedLines,
                    std::size_t maxQueuedBytes, StdinLineReader::ReadFunction read = {});
    ~StdinLineSource() override; // stops and joins the reader thread
    StdinLineSource(const StdinLineSource&) = delete;
    StdinLineSource& operator=(const StdinLineSource&) = delete;
    StdinLineSource(StdinLineSource&&) = delete;
    StdinLineSource& operator=(StdinLineSource&&) = delete;

    [[nodiscard]] std::optional<Line> takeLine() override;
    [[nodiscard]] bool atEnd() const override { return m_end; }
    void setWakeup(std::function<void()> wakeup) override;

private:
    void wake();

    std::mutex m_mutex;
    std::function<void()> m_wakeup; // guarded by m_mutex
    bool m_end = false;             // owner thread
    // Declared last: destroyed (stopped and joined) first, while the members above still exist.
    std::unique_ptr<StdinLineReader> m_reader;
};

} // namespace hio::cli
