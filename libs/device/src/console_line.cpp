#include "hio/device/console_line.h"

#include <optional>
#include <string_view>

namespace hio::device {

std::string_view channelId(Channel channel) noexcept {
    return channel == Channel::Log ? "log" : "shell";
}

std::string_view directionId(Direction direction) noexcept {
    switch (direction) {
    case Direction::Out:
        return "out";
    case Direction::In:
        return "in";
    case Direction::Marker:
        return "marker";
    }
    return "out";
}

std::optional<std::string_view> levelId(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::None:
        return std::nullopt;
    case LogLevel::Debug:
        return "dbg";
    case LogLevel::Info:
        return "inf";
    case LogLevel::Warning:
        return "wrn";
    case LogLevel::Error:
        return "err";
    }
    return std::nullopt;
}

} // namespace hio::device
