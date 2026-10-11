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
#include <optional>
#include <span>
#include <stop_token>
#include <string>
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

// Splits bytes into lines ("\n", one trailing "\r" removed); a line longer than `maxBytes` is cut
// there and the rest of it is dropped.
class LineSplitter {
public:
    explicit LineSplitter(std::size_t maxBytes) : m_maxBytes(maxBytes) {}

    [[nodiscard]] std::vector<std::string> feed(std::span<const char> bytes) {
        std::vector<std::string> lines;
        for (const char c : bytes) {
            if (c == '\n') {
                if (m_line.ends_with('\r')) {
                    m_line.pop_back();
                }
                lines.push_back(std::exchange(m_line, {}));
                m_cut = false;
            } else if (!m_cut) {
                m_line += c;
                m_cut = m_line.size() >= m_maxBytes;
            }
        }
        return lines;
    }

    // The unterminated last line, if any.
    [[nodiscard]] std::optional<std::string> rest() {
        if (m_line.empty()) {
            return std::nullopt;
        }
        return std::exchange(m_line, {});
    }

private:
    std::size_t m_maxBytes;
    std::string m_line;
    bool m_cut = false; // the current line reached m_maxBytes; drop until its end
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

StdinLineReader::StdinLineReader() : StdinLineReader(ReadFunction(readAvailable)) {}

StdinLineReader::StdinLineReader(ReadFunction read)
    : m_read(std::move(read)), m_thread([this](const std::stop_token& stop) { run(stop); }) {}

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
    return m_lines.pop({}, std::chrono::milliseconds(0));
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
            std::ignore = m_lines.push(std::nullopt, stop);
        } catch (...) { // NOLINT(bugprone-empty-catch): the queue is closed by the destructor
        }
    }
}

void StdinLineReader::readLines(const std::stop_token& stop) {
    std::array<char, 4096> buffer{};
    LineSplitter splitter(maxLineBytes);
    while (!stop.stop_requested()) {
        std::size_t count = 0;
        const ReadResult result = m_read(buffer, count);
        if (result == ReadResult::NoData) {
            continue;
        }
        std::vector<std::string> lines = splitter.feed(std::span(buffer).first(count));
        if (result == ReadResult::End) {
            if (auto rest = splitter.rest()) {
                lines.push_back(std::move(*rest));
            }
        }
        for (std::string& line : lines) {
            if (!m_lines.push(std::move(line), stop)) {
                return; // stopped or closed
            }
        }
        if (result == ReadResult::End) {
            // The end-of-input marker; if stopped meanwhile, nobody waits for it any more.
            std::ignore = m_lines.push(std::nullopt, stop);
            return;
        }
    }
}

} // namespace hio::cli
