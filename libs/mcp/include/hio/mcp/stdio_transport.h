#pragma once

// StdioTransport - MCP over newline-delimited JSON (plan/features/0013 "stdio transport", ADR-0097,
// ADR-0102, ADR-0154).
//
// Input comes from an ILineSource (the CLI's StdinLineReader implements it: an owned reader thread,
// a bounded line queue of Limits::stdinQueueLines lines, lines up to Limits::maxStdioLineBytes, an
// "overlong" marker for longer lines). The reader thread only calls the wake-up function; the
// transport then takes the lines on the owner thread (at most Limits::stdioLinesPerWakeup per
// event-loop turn) and passes each one to McpServer::handle(). An overlong line is answered with a
// JSON-RPC parse error (-32700, no id). Empty lines are ignored.
//
// Output: one JSON message + "\n" per write to the injected QIODevice (stdout in the CLI, a QBuffer
// in tests), flushed for QFileDevices, on the owner thread only. Nothing else may write to it.
//
// End of input: inputClosed() after the last line was handled (the CLI then calls
// McpServer::stop() and leaves its event loop). A failed write: outputFailed() once; the channel
// counts as closed afterwards (modern calls are cancelled).
//
// Thread safety: owner thread only, except that the ILineSource calls the wake-up from its thread.

#include <QByteArray>
#include <QIODevice>
#include <QObject>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace hio::mcp {

class McpServer;

class ILineSource {
public:
    struct Line {
        QByteArray text;       // without the newline
        bool overlong = false; // the line exceeded the reader's limit and was dropped
    };

    ILineSource() = default;
    ILineSource(const ILineSource&) = delete;
    ILineSource& operator=(const ILineSource&) = delete;
    ILineSource(ILineSource&&) = delete;
    ILineSource& operator=(ILineSource&&) = delete;
    virtual ~ILineSource() = default;

    // Owner thread. The next queued line, or nullopt when the queue is empty (taking a line frees
    // queue space for the blocked reader).
    [[nodiscard]] virtual std::optional<Line> takeLine() = 0;
    // Owner thread. True when the input ended (EOF or a read error) and every line was taken.
    [[nodiscard]] virtual bool atEnd() const = 0;
    // Owner thread. `wakeup` is called from the reader thread after each queued line and at the end
    // of input. Implementations serialize the call with replacement: once setWakeup() returns, the
    // previous function is never called again (StdioTransport's destructor relies on it).
    virtual void setWakeup(std::function<void()> wakeup) = 0;
};

class StdioChannel;

class StdioTransport : public QObject {
    Q_OBJECT

public:
    // `server`, `input` and `output` must outlive the transport. `output` must be open for writing.
    StdioTransport(McpServer& server, ILineSource& input, QIODevice& output,
                   QObject* parent = nullptr);
    ~StdioTransport() override;
    StdioTransport(const StdioTransport&) = delete;
    StdioTransport& operator=(const StdioTransport&) = delete;
    StdioTransport(StdioTransport&&) = delete;
    StdioTransport& operator=(StdioTransport&&) = delete;

    // Installs the wake-up and handles lines already queued.
    void start();

    [[nodiscard]] std::uint64_t connectionId() const noexcept { return m_connectionId; }

signals:
    void inputClosed();
    void outputFailed();

private:
    friend class StdioChannel;

    void drain();
    void write(const QByteArray& message);

    McpServer& m_server;
    ILineSource& m_input;
    QIODevice& m_output;
    std::shared_ptr<StdioChannel> m_channel;
    std::uint64_t m_connectionId;
    std::atomic<bool> m_wakePending{false};
    bool m_inputClosedEmitted = false;
    bool m_outputFailed = false;
};

} // namespace hio::mcp
