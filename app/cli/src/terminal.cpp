#include "terminal.h"

#include "cli_log.h"
#include "hio/core/bounded_queue.h"

#include <QtEnvironmentVariables>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#ifdef _WIN32
// NOMINMAX and WIN32_LEAN_AND_MEAN are set by app/cli/CMakeLists.txt.
#include <cstdio>
#include <io.h>
#include <windows.h>
#else
#include <sys/poll.h>
#include <sys/types.h> // ssize_t
#include <unistd.h>
#endif

namespace hio::cli {

namespace {

constexpr auto pollInterval = std::chrono::milliseconds(50);

using ReadResult = StdinLineReader::ReadStatus;

// Reads what is available from stdin into `buffer` without blocking longer than pollInterval.
ReadResult readAvailable(std::span<char> buffer, std::size_t& count) {
    count = 0;
#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == nullptr || input == INVALID_HANDLE_VALUE) {
        return ReadResult::End;
    }
    DWORD available = 0;
    switch (GetFileType(input)) {
    case FILE_TYPE_PIPE:
        if (PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr) == 0) {
            return ReadResult::End; // the writer closed the pipe
        }
        if (available == 0) {
            std::this_thread::sleep_for(pollInterval);
            return ReadResult::NoData;
        }
        break;
    case FILE_TYPE_CHAR:
        if (WaitForSingleObject(input, static_cast<DWORD>(pollInterval.count())) != WAIT_OBJECT_0) {
            return ReadResult::NoData;
        }
        break;
    default: // a file: reading never blocks
        break;
    }
    DWORD read = 0;
    const DWORD wanted = available > 0
                             ? std::min<DWORD>(available, static_cast<DWORD>(buffer.size()))
                             : static_cast<DWORD>(buffer.size());
    if (ReadFile(input, buffer.data(), wanted, &read, nullptr) == 0 || read == 0) {
        return ReadResult::End;
    }
    count = read;
    return ReadResult::Data;
#else
    pollfd descriptor{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
    const int ready = ::poll(&descriptor, 1, static_cast<int>(pollInterval.count()));
    if (ready == 0) {
        return ReadResult::NoData;
    }
    if (ready < 0) {
        return errno == EINTR ? ReadResult::NoData : ReadResult::End;
    }
    const ssize_t read = ::read(STDIN_FILENO, buffer.data(), buffer.size());
    if (read < 0) {
        return errno == EINTR || errno == EAGAIN ? ReadResult::NoData : ReadResult::End;
    }
    if (read == 0) {
        return ReadResult::End;
    }
    count = static_cast<std::size_t>(read);
    return ReadResult::Data;
#endif
}

// Splits bytes into lines ("\n", one trailing "\r" removed). A line longer than `maxBytes` is cut
// there (Truncate) or dropped as a whole and reported (DropAndReport); the rest of it is skipped,
// so at most maxBytes + 1 bytes are buffered.
class LineSplitter {
public:
    LineSplitter(std::size_t maxBytes, OverlongLinePolicy policy)
        : m_maxBytes(std::max<std::size_t>(maxBytes, 1)), m_policy(policy) {}

    [[nodiscard]] std::vector<StdinItem> feed(std::span<const char> bytes) {
        std::vector<StdinItem> items;
        std::string_view rest(bytes.data(), bytes.size());
        while (!rest.empty()) {
            const std::size_t newline = rest.find('\n');
            append(rest.substr(0, newline));
            if (newline == std::string_view::npos) {
                break;
            }
            items.push_back(endLine());
            rest.remove_prefix(newline + 1);
        }
        return items;
    }

    // The unterminated last line, if any.
    [[nodiscard]] std::optional<StdinItem> rest() {
        if (!m_skipping && m_line.empty()) {
            return std::nullopt;
        }
        if (m_policy == OverlongLinePolicy::DropAndReport &&
            (m_skipping || m_line.size() > m_maxBytes)) {
            reset();
            return StdinItem{.kind = StdinItem::Kind::Overlong, .text = {}};
        }
        m_skipping = false;
        return StdinItem{.kind = StdinItem::Kind::Line, .text = std::exchange(m_line, {})};
    }

private:
    void append(std::string_view part) {
        if (m_skipping) {
            return;
        }
        if (m_policy == OverlongLinePolicy::Truncate) {
            m_line.append(part.substr(0, m_maxBytes - m_line.size()));
            m_skipping = m_line.size() >= m_maxBytes;
            return;
        }
        // DropAndReport: one byte more than the limit may be a trailing "\r".
        if (m_line.size() + part.size() > m_maxBytes + 1) {
            m_line.clear();
            m_skipping = true;
            return;
        }
        m_line.append(part);
    }

    StdinItem endLine() {
        if (m_line.ends_with('\r')) {
            m_line.pop_back();
        }
        if (m_policy == OverlongLinePolicy::DropAndReport &&
            (m_skipping || m_line.size() > m_maxBytes)) {
            reset();
            return StdinItem{.kind = StdinItem::Kind::Overlong, .text = {}};
        }
        m_skipping = false;
        return StdinItem{.kind = StdinItem::Kind::Line, .text = std::exchange(m_line, {})};
    }

    void reset() {
        m_line.clear();
        m_skipping = false;
    }

    std::size_t m_maxBytes;
    OverlongLinePolicy m_policy;
    std::string m_line;
    // Truncate: the line reached m_maxBytes; DropAndReport: the line is too long. Either way the
    // bytes up to the end of the line are dropped.
    bool m_skipping = false;
};

} // namespace

bool stdinIsTerminal() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

bool stdoutIsTerminal() {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

bool stderrIsTerminal() {
#ifdef _WIN32
    return _isatty(_fileno(stderr)) != 0;
#else
    return isatty(STDERR_FILENO) != 0;
#endif
}

bool stdoutColors() { return stdoutIsTerminal() && qEnvironmentVariableIsEmpty("NO_COLOR"); }

bool askYesNo(const std::string& question) {
    std::cerr << question << std::flush;
    std::string answer;
    if (!std::getline(std::cin, answer)) {
        return false;
    }
    std::ranges::transform(answer, answer.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return answer == "y" || answer == "yes";
}

StdinLineReader::StdinLineReader() : StdinLineReader(StdinReaderOptions{}) {}

StdinLineReader::StdinLineReader(ReadFunction read)
    : StdinLineReader(StdinReaderOptions{}, std::move(read)) {}

StdinLineReader::StdinLineReader(StdinReaderOptions options, ReadFunction read)
    : m_options(std::move(options)), m_read(read ? std::move(read) : ReadFunction(readAvailable)),
      m_lines(std::max<std::size_t>(m_options.maxQueuedLines, 1), OverflowPolicy::Block),
      m_thread([this](const std::stop_token& stop) { run(stop); }) {}

StdinLineReader::~StdinLineReader() {
    m_thread.request_stop();
    m_lines.close(); // wakes a reader waiting for queue space
#ifdef _WIN32
    // A console ReadFile can block until Enter; cancel it until the thread has finished
    // (review R17; bounded: at most 100 x 10 ms, then join waits for the read to return).
    for (int attempt = 0; attempt < 100 && !m_finished.load() && m_thread.joinable(); ++attempt) {
        CancelSynchronousIo(static_cast<HANDLE>(m_thread.native_handle()));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#endif
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

std::optional<std::optional<std::string>> StdinLineReader::poll() {
    while (auto item = take()) {
        switch (item->kind) {
        case StdinItem::Kind::Line:
            return std::optional<std::string>(std::move(item->text));
        case StdinItem::Kind::End:
            return std::optional<std::string>();
        case StdinItem::Kind::Overlong:
            break; // skipped (never queued by a Truncate reader)
        }
    }
    return std::nullopt;
}

std::optional<StdinItem> StdinLineReader::take() {
    std::optional<StdinItem> item = m_lines.pop({}, std::chrono::milliseconds(0));
    if (item && item->kind == StdinItem::Kind::Line && m_options.maxQueuedBytes > 0) {
        {
            const std::scoped_lock lock(m_bytesMutex);
            m_queuedBytes -= std::min(m_queuedBytes, item->text.size());
        }
        m_bytesFreed.notify_all();
    }
    return item;
}

bool StdinLineReader::push(StdinItem item, const std::stop_token& stop) {
    const std::size_t bytes = item.kind == StdinItem::Kind::Line ? item.text.size() : 0;
    const bool budgeted = m_options.maxQueuedBytes > 0 && bytes > 0;
    if (budgeted) {
        // ADR-0163: wait for queue bytes (the destructor's stop request ends the wait).
        std::unique_lock lock(m_bytesMutex);
        if (!m_bytesFreed.wait(lock, stop, [this, bytes] {
                return m_queuedBytes == 0 || m_queuedBytes + bytes <= m_options.maxQueuedBytes;
            })) {
            return false; // stopped
        }
        m_queuedBytes += bytes;
    }
    if (!m_lines.push(std::move(item), stop)) {
        if (budgeted) {
            const std::scoped_lock lock(m_bytesMutex);
            m_queuedBytes -= std::min(m_queuedBytes, bytes);
        }
        return false; // stopped or closed
    }
    if (m_options.notify) {
        m_options.notify();
    }
    return true;
}

void StdinLineReader::run(const std::stop_token& stop) {
    // Marks the end of run() for the destructor (also on an early return).
    class FinishedFlag {
    public:
        explicit FinishedFlag(std::atomic<bool>& flag) : m_flag(&flag) {}
        FinishedFlag(const FinishedFlag&) = delete;
        FinishedFlag& operator=(const FinishedFlag&) = delete;
        FinishedFlag(FinishedFlag&&) = delete;
        FinishedFlag& operator=(FinishedFlag&&) = delete;
        ~FinishedFlag() { m_flag->store(true); }

    private:
        std::atomic<bool>* m_flag;
    };
    const FinishedFlag finished(m_finished);
    try {
        readLines(stop);
    } catch (...) {
        // No exception escapes the thread (review R27): it is the end of input.
        logThreadException("stdin reader");
        try {
            std::ignore = push(StdinItem{.kind = StdinItem::Kind::End, .text = {}}, stop);
        } catch (...) { // NOLINT(bugprone-empty-catch): the queue is closed by the destructor
        }
    }
}

void StdinLineReader::readLines(const std::stop_token& stop) {
    std::array<char, 4096> buffer{};
    LineSplitter splitter(m_options.maxLineBytes, m_options.overlong);
    while (!stop.stop_requested()) {
        std::size_t count = 0;
        const ReadResult result = m_read(buffer, count);
        if (result == ReadResult::NoData) {
            continue;
        }
        std::vector<StdinItem> items = splitter.feed(std::span(buffer).first(count));
        if (result == ReadResult::End) {
            if (auto rest = splitter.rest()) {
                items.push_back(std::move(*rest));
            }
        }
        for (StdinItem& item : items) {
            if (!push(std::move(item), stop)) {
                return; // stopped or closed
            }
        }
        if (result == ReadResult::End) {
            // The end-of-input marker; if stopped meanwhile, nobody waits for it any more.
            std::ignore = push(StdinItem{.kind = StdinItem::Kind::End, .text = {}}, stop);
            return;
        }
    }
}

} // namespace hio::cli
