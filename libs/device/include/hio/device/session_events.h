#pragma once

// Events of a DeviceSession (plan/features/0002-m1-jlink-rtt.md, ADR-0031).

#include "hio/core/error.h"
#include "hio/device/console_line.h"

#include <QMetaType>

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

namespace hio::device {

enum class ConnectionState : std::uint8_t {
    Connecting,   // the worker loads the library, opens the probe, connects and attaches RTT
    Connected,    // RTT attached and the link works
    Disconnected, // the link failed (error says why); auto-reconnect or reconnect() may recover
    Stopped,      // fatal error, or suspended on purpose (no error); only reconnect() recovers
};

struct ConnectionEvent {
    ConnectionState state = ConnectionState::Connecting;
    std::optional<Error> error;
};

// Events were lost because the event queue was full (it drops the oldest, ADR-0031).
struct LinesDropped {
    std::uint64_t count = 0;
};

struct Warning {
    Error error;
};

using SessionEvent = std::variant<ConsoleLine, ConnectionEvent, LinesDropped, Warning>;

// "connecting", "connected", "disconnected", "stopped" (JSON).
[[nodiscard]] std::string_view connectionStateId(ConnectionState state) noexcept;

} // namespace hio::device

Q_DECLARE_METATYPE(hio::device::ConnectionEvent)
Q_DECLARE_METATYPE(hio::Error)
