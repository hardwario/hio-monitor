#pragma once

// How console session events are shown (plan/features/0002-m1-jlink-rtt.md, ADR-0026):
// NDJSON event objects for --json and human text lines.

#include "hio/core/error.h"
#include "hio/device/console_line.h"
#include "hio/device/session_events.h"

#include <QJsonObject>

#include <cstdint>
#include <string>

namespace hio::cli {

// {"type":"line","seq","time","channel","dir","level","text"}; time = ISO 8601 UTC with ms.
[[nodiscard]] QJsonObject lineEventJson(const device::ConsoleLine& line);
// {"type":"connection","state","error": {...} | null,"transport":"jlink"|"ble"} and, for BLE,
// "mtu": number | null (plan 0011 Q5)
[[nodiscard]] QJsonObject connectionEventJson(const device::ConnectionEvent& event);
// {"type":"dropped","count"}
[[nodiscard]] QJsonObject droppedEventJson(std::uint64_t count);
// {"type":"warning","error": {...}}
[[nodiscard]] QJsonObject warningEventJson(const Error& error);

// A device line for the terminal; log lines are colored by level when `colors` is set
// (<err> red, <wrn> yellow, <dbg> dim, <inf> default).
[[nodiscard]] std::string humanLine(const device::ConsoleLine& line, bool colors);
// "-- connected --", "-- connected (Bluetooth, MTU 247) --", "-- disconnected: <message> --", ...
[[nodiscard]] std::string humanConnection(const device::ConnectionEvent& event);
// "Warning: <message> <suggestion>"
[[nodiscard]] std::string humanWarning(const Error& error);

} // namespace hio::cli
