#pragma once

// Small terminal helpers of the CLI (TTY detection, a yes/no question, non-blocking stdin lines).

#include "hio/core/bounded_queue.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <thread>

namespace hio::cli {

[[nodiscard]] bool stdinIsTerminal();
[[nodiscard]] bool stdoutIsTerminal();
[[nodiscard]] bool stderrIsTerminal();
// Colors on stdout: a TTY and NO_COLOR not set.
[[nodiscard]] bool stdoutColors();

// Asks on stderr and reads one line from stdin; true for "y" / "yes" (any case).
[[nodiscard]] bool askYesNo(const std::string& question);

// What StdinLineReader does with a line longer than StdinReaderOptions::maxLineBytes (ADR-0154).
enum class OverlongLinePolicy : std::uint8_t {
    Truncate,      // the line is cut at maxLineBytes, the rest of it is dropped (the console)
    DropAndReport, // the whole line is dropped and an Overlong item is delivered instead (hio mcp)
};

// StdinLineReader parameters (ADR-0154). The defaults are the console's values.
struct StdinReaderOptions {
    // Longest line in bytes, without the "\n" and one trailing "\r". Contract: >= 1.
    std::size_t maxLineBytes = 4096;
    // Queue bound in items (lines, markers, the end of input). Contract: >= 1. The reader thread
    // waits while the queue is full (backpressure).
    std::size_t maxQueuedLines = 256;
    // ADR-0163: optional byte budget of the queued lines (their text); 0 = none (the console).
    // The reader thread also waits while a new line would exceed it; a line is always queued when
    // the queue holds no line bytes, so one line longer than the budget cannot block forever.
    std::size_t maxQueuedBytes = 0;
    OverlongLinePolicy overlong = OverlongLinePolicy::Truncate;
    // Optional; called on the reader thread after each queued item (also the Overlong marker and
    // the end of input). It must be thread-safe, return quickly and must not touch the reader
    // (hio mcp posts a queued wake-up to its owner thread). An exception from it ends the input.
    std::function<void()> notify;
};

// One item delivered by StdinLineReader::take().
struct StdinItem {
    enum class Kind : std::uint8_t {
        Line,     // `text` holds the line (without "\n" and one trailing "\r")
        Overlong, // DropAndReport: a line longer than maxLineBytes was dropped
        End,      // end of input (EOF, a read error, or an exception in the reader thread)
    };
    Kind kind = Kind::Line;
    std::string text;

    friend bool operator==(const StdinItem&, const StdinItem&) = default;
};

// Reads stdin lines on an owned thread that never blocks indefinitely (POSIX poll / Windows
// handle waits, 50 ms), so it stops promptly when destroyed. Lines are delivered through a
// bounded queue (StdinReaderOptions::maxQueuedLines items and optionally maxQueuedBytes bytes of
// line text; the reader waits when either is reached); an
// End item (poll(): nullopt) marks the end of input. Lines longer than maxLineBytes are cut there
// (Truncate) or replaced by an Overlong item (DropAndReport); either way the rest of the line is
// dropped and at most maxLineBytes + 1 bytes of a line are buffered.
// An exception in the reader thread (or in `read`) is logged and ends the input (review R27).
// Thread safety: poll() / take() from one consumer thread; the reader thread only pushes.
class StdinLineReader {
public:
    // The console's values (ADR-0154; the defaults of StdinReaderOptions).
    static constexpr std::size_t maxQueuedLines = 256;
    static constexpr std::size_t maxLineBytes = 4096;

    enum class ReadStatus : std::uint8_t {
        Data,   // `count` bytes were read
        NoData, // nothing within the poll interval
        End,    // end of input
    };
    // Reads what is available into `buffer` (waiting at most about 50 ms), sets `count`.
    using ReadFunction = std::function<ReadStatus(std::span<char> buffer, std::size_t& count)>;

    StdinLineReader(); // reads stdin with the console's options
    // Reads through `read` instead of stdin (tests), the console's options.
    explicit StdinLineReader(ReadFunction read);
    // ADR-0154: `read` empty = stdin.
    explicit StdinLineReader(StdinReaderOptions options, ReadFunction read = {});
    ~StdinLineReader(); // requests stop and joins
    StdinLineReader(const StdinLineReader&) = delete;
    StdinLineReader& operator=(const StdinLineReader&) = delete;
    StdinLineReader(StdinLineReader&&) = delete;
    StdinLineReader& operator=(StdinLineReader&&) = delete;

    // A line, nullopt (end of input) wrapped in an optional, or nothing yet (outer nullopt).
    // Overlong items are skipped (meant for Truncate readers such as the console).
    [[nodiscard]] std::optional<std::optional<std::string>> poll();
    // Non-blocking: the next item, or nullopt when none is queued (taking one frees queue space
    // for the waiting reader thread). Nothing follows an End item.
    [[nodiscard]] std::optional<StdinItem> take();

private:
    void run(const std::stop_token& stop);
    void readLines(const std::stop_token& stop);
    [[nodiscard]] bool push(StdinItem item, const std::stop_token& stop);

    StdinReaderOptions m_options;
    ReadFunction m_read;
    BoundedQueue<StdinItem> m_lines;
    // The byte budget (maxQueuedBytes > 0 only): text bytes of the queued Line items.
    std::mutex m_bytesMutex;
    std::condition_variable_any m_bytesFreed;
    std::size_t m_queuedBytes = 0;       // guarded by m_bytesMutex
    std::atomic<bool> m_finished{false}; // run() returned (Windows: ends CancelSynchronousIo)
    std::jthread m_thread;               // declared last: stopped and joined first
};

} // namespace hio::cli
