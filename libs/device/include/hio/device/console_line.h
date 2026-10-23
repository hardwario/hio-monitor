#pragma once

// One line of device console output or input (plan/features/0002-m1-jlink-rtt.md, ADR-0063).

#include <QMetaType>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hio::device {

enum class Channel : std::uint8_t {
    Shell, // RTT terminal buffer: shell output and the commands sent
    Log,   // RTT logger buffer (and '#'-prefixed shell lines when the profile says so)
};

enum class Direction : std::uint8_t {
    Out,    // from the device
    In,     // a command sent to the device (echoed after it was written)
    Marker, // a marker line added by the GUI (ADR-0063); never produced by the session or CLI
};

enum class LogLevel : std::uint8_t {
    None, // no Zephyr level tag (shell output, commands)
    Debug,
    Info,
    Warning,
    Error,
    // Plan 0021 (ADR-0171): a Zephyr LOG_HEXDUMP continuation line (no tag of its own; the header
    // line keeps its level). Hidden by default in the GUI log view (chip H).
    Hexdump,
};

struct ConsoleLine {
    std::uint64_t seq = 0; // position in the ConsoleLog (assigned by ConsoleLog::append)
    Channel channel = Channel::Shell;
    Direction dir = Direction::Out;
    std::chrono::system_clock::time_point time;
    LogLevel level = LogLevel::None;
    std::string text; // UTF-8, without line terminator and ANSI CSI sequences
};

// Stable lowercase ids used in JSON and console files.
[[nodiscard]] std::string_view channelId(Channel channel) noexcept;       // "shell", "log"
[[nodiscard]] std::string_view directionId(Direction direction) noexcept; // "out", "in", "marker"
// "dbg", "inf", "wrn", "err", "hex" (ADR-0171), or none.
[[nodiscard]] std::optional<std::string_view> levelId(LogLevel level) noexcept;

} // namespace hio::device

Q_DECLARE_METATYPE(hio::device::ConsoleLine)
