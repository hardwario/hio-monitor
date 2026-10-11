#pragma once

// Small terminal helpers of the CLI (TTY detection, a yes/no question, non-blocking stdin lines).

#include "hio/core/bounded_queue.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
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

// Reads stdin lines on an owned thread that never blocks indefinitely (POSIX poll / Windows
// handle waits, 50 ms), so it stops promptly when destroyed. Lines are delivered through a
// bounded queue (capacity maxQueuedLines, the reader waits when it is full); nullopt marks the end
// of input. Lines longer than maxLineBytes are cut there (the rest of the line is dropped).
// An exception in the reader thread (or in `read`) is logged and ends the input (review R27).
class StdinLineReader {
public:
    static constexpr std::size_t maxQueuedLines = 256;
    static constexpr std::size_t maxLineBytes = 4096;

    enum class ReadStatus : std::uint8_t {
        Data,   // `count` bytes were read
        NoData, // nothing within the poll interval
        End,    // end of input
    };
    // Reads what is available into `buffer` (waiting at most about 50 ms), sets `count`.
    using ReadFunction = std::function<ReadStatus(std::span<char> buffer, std::size_t& count)>;

    StdinLineReader(); // reads stdin
    // Reads through `read` instead of stdin (tests).
    explicit StdinLineReader(ReadFunction read);
    ~StdinLineReader(); // requests stop and joins
    StdinLineReader(const StdinLineReader&) = delete;
    StdinLineReader& operator=(const StdinLineReader&) = delete;
    StdinLineReader(StdinLineReader&&) = delete;
    StdinLineReader& operator=(StdinLineReader&&) = delete;

    // A line, nullopt (end of input) wrapped in an optional, or nothing yet (outer nullopt).
    [[nodiscard]] std::optional<std::optional<std::string>> poll();

private:
    void run(const std::stop_token& stop);
    void readLines(const std::stop_token& stop);

    ReadFunction m_read;
    BoundedQueue<std::optional<std::string>> m_lines{maxQueuedLines, OverflowPolicy::Block};
    std::atomic<bool> m_finished{false}; // run() returned (Windows: ends CancelSynchronousIo)
    std::jthread m_thread;               // declared last: stopped and joined first
};

} // namespace hio::cli
