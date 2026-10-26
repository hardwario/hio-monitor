#include "console_events.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/device/console_line.h"
#include "hio/device/session_events.h"

#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringLiteral>
#include <QTimeZone>
#include <QtCore/qnamespace.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace hio::cli {

namespace {

constexpr std::string_view red = "\x1b[31m";
constexpr std::string_view yellow = "\x1b[33m";
constexpr std::string_view dim = "\x1b[2m";
constexpr std::string_view reset = "\x1b[0m";

QString isoTime(std::chrono::system_clock::time_point time) {
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
    return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC).toString(Qt::ISODateWithMs);
}

QString latin1(std::string_view text) { return toQString(text); }

} // namespace

QJsonObject lineEventJson(const device::ConsoleLine& line) {
    QJsonObject event;
    event.insert(QStringLiteral("type"), QStringLiteral("line"));
    event.insert(QStringLiteral("seq"), static_cast<double>(line.seq));
    event.insert(QStringLiteral("time"), isoTime(line.time));
    event.insert(QStringLiteral("channel"), latin1(device::channelId(line.channel)));
    event.insert(QStringLiteral("dir"), latin1(device::directionId(line.dir)));
    const auto level = device::levelId(line.level);
    event.insert(QStringLiteral("level"),
                 level ? QJsonValue(latin1(*level)) : QJsonValue(QJsonValue::Null));
    event.insert(QStringLiteral("text"), toQString(line.text));
    return event;
}

QJsonObject connectionEventJson(const device::ConnectionEvent& event) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("connection"));
    object.insert(QStringLiteral("state"), latin1(device::connectionStateId(event.state)));
    object.insert(QStringLiteral("error"),
                  event.error ? QJsonValue(toJson(*event.error)) : QJsonValue(QJsonValue::Null));
    // M5 (plan 0011 Q5): the transport on every event, the MTU on BLE events.
    object.insert(QStringLiteral("transport"), latin1(device::transportId(event.transport)));
    if (event.transport == device::Transport::Ble) {
        object.insert(QStringLiteral("mtu"), event.mtu ? QJsonValue(static_cast<int>(*event.mtu))
                                                       : QJsonValue(QJsonValue::Null));
    }
    return object;
}

QJsonObject droppedEventJson(std::uint64_t count) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("dropped"));
    object.insert(QStringLiteral("count"), static_cast<double>(count));
    return object;
}

QJsonObject warningEventJson(const Error& error) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("warning"));
    object.insert(QStringLiteral("error"), toJson(error));
    return object;
}

std::string humanLine(const device::ConsoleLine& line, bool colors) {
    if (!colors || line.channel != device::Channel::Log) {
        return line.text;
    }
    std::string_view color;
    switch (line.level) {
    case device::LogLevel::Error:
        color = red;
        break;
    case device::LogLevel::Warning:
        color = yellow;
        break;
    case device::LogLevel::Debug:
        color = dim;
        break;
    case device::LogLevel::Info:
    case device::LogLevel::None:
        return line.text;
    }
    return std::string(color) + line.text + std::string(reset);
}

std::string humanConnection(const device::ConnectionEvent& event) {
    std::string text = "-- ";
    text += device::connectionStateId(event.state);
    if (event.state == device::ConnectionState::Connected &&
        event.transport == device::Transport::Ble) {
        text += event.mtu ? std::format(" (Bluetooth, MTU {})", *event.mtu) : " (Bluetooth)";
    }
    if (event.error) {
        text += ": " + event.error->message;
    }
    return text + " --";
}

std::string humanWarning(const Error& error) {
    std::string text = "Warning: " + error.message;
    if (!error.suggestion.empty()) {
        text += " " + error.suggestion;
    }
    return text;
}

} // namespace hio::cli
