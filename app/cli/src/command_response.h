#pragma once

// The bounded response of `hio chester app command` (plan/features/0002-m1-jlink-rtt.md, Bounds,
// review R42): the shell output and log lines received after the command was sent, at most
// maxLines lines (output + log) and maxBytes bytes of text in total. Beyond the bound the first
// lines are kept, later lines are not collected and truncated() is set.

#include <cstddef>
#include <string>
#include <vector>

namespace hio::cli {

class CommandResponse {
public:
    static constexpr std::size_t maxLines = 10'000;
    static constexpr std::size_t maxBytes = 4ULL * 1024 * 1024;

    // Adds a line unless the bound is reached (then sets truncated()). Returns whether it was kept.
    bool addOutput(std::string text) { return add(m_output, std::move(text)); }
    bool addLog(std::string text) { return add(m_log, std::move(text)); }

    [[nodiscard]] const std::vector<std::string>& output() const noexcept { return m_output; }
    [[nodiscard]] const std::vector<std::string>& log() const noexcept { return m_log; }
    // Lines kept (output + log).
    [[nodiscard]] std::size_t lines() const noexcept { return m_output.size() + m_log.size(); }
    [[nodiscard]] std::size_t bytes() const noexcept { return m_bytes; }
    [[nodiscard]] bool truncated() const noexcept { return m_truncated; }

private:
    bool add(std::vector<std::string>& target, std::string text);

    std::vector<std::string> m_output; // bounded: maxLines, maxBytes (with m_log)
    std::vector<std::string> m_log;
    std::size_t m_bytes = 0;
    bool m_truncated = false;
};

} // namespace hio::cli
