// The NDJSON console events and their human text (plan/features/0002, `--json` console stream).

#include "console_events.h"
#include "hio/core/error.h"
#include "hio/device/console_line.h"
#include "hio/device/session_events.h"

#include <QJsonObject>
#include <QString>

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>

namespace {

using hio::device::Channel;
using hio::device::ConnectionEvent;
using hio::device::ConnectionState;
using hio::device::ConsoleLine;
using hio::device::Direction;
using hio::device::LogLevel;

ConsoleLine logLine(LogLevel level) {
    ConsoleLine line;
    line.seq = 17;
    line.channel = Channel::Log;
    line.dir = Direction::Out;
    line.time = std::chrono::system_clock::time_point(std::chrono::milliseconds(1791549296789));
    line.level = level;
    line.text = "[00:00:01.234,000] <inf> app: Build version: v3.1.0";
    return line;
}

TEST(ConsoleEvents, LineJson) {
    const QJsonObject event = hio::cli::lineEventJson(logLine(LogLevel::Info));
    EXPECT_EQ(event.value(QStringLiteral("type")).toString(), QStringLiteral("line"));
    EXPECT_EQ(event.value(QStringLiteral("seq")).toInteger(), 17);
    EXPECT_EQ(event.value(QStringLiteral("time")).toString(),
              QStringLiteral("2026-10-09T12:34:56.789Z"));
    EXPECT_EQ(event.value(QStringLiteral("channel")).toString(), QStringLiteral("log"));
    EXPECT_EQ(event.value(QStringLiteral("dir")).toString(), QStringLiteral("out"));
    EXPECT_EQ(event.value(QStringLiteral("level")).toString(), QStringLiteral("inf"));
    EXPECT_EQ(event.value(QStringLiteral("text")).toString(),
              QStringLiteral("[00:00:01.234,000] <inf> app: Build version: v3.1.0"));
    EXPECT_TRUE(
        hio::cli::lineEventJson(logLine(LogLevel::None)).value(QStringLiteral("level")).isNull());
    // Plan 0021 (ADR-0171): hexdump continuation lines.
    EXPECT_EQ(hio::cli::lineEventJson(logLine(LogLevel::Hexdump)).value(QStringLiteral("level")),
              QStringLiteral("hex"));
}

TEST(ConsoleEvents, ConnectionJson) {
    const QJsonObject connected =
        hio::cli::connectionEventJson(ConnectionEvent{.state = ConnectionState::Connected,
                                                      .error = std::nullopt,
                                                      .targetMillivolts = std::nullopt});
    EXPECT_EQ(connected.value(QStringLiteral("type")).toString(), QStringLiteral("connection"));
    EXPECT_EQ(connected.value(QStringLiteral("state")).toString(), QStringLiteral("connected"));
    EXPECT_TRUE(connected.value(QStringLiteral("error")).isNull());
    const QJsonObject disconnected = hio::cli::connectionEventJson(
        ConnectionEvent{.state = ConnectionState::Disconnected,
                        .error = hio::makeError(hio::ErrorCode::TargetNoPower),
                        .targetMillivolts = std::nullopt});
    EXPECT_EQ(disconnected.value(QStringLiteral("state")).toString(),
              QStringLiteral("disconnected"));
    EXPECT_EQ(disconnected.value(QStringLiteral("error"))
                  .toObject()
                  .value(QStringLiteral("code"))
                  .toString(),
              QStringLiteral("target.no_power"));
}

TEST(ConsoleEvents, DroppedAndWarningJson) {
    const QJsonObject dropped = hio::cli::droppedEventJson(120);
    EXPECT_EQ(dropped.value(QStringLiteral("type")).toString(), QStringLiteral("dropped"));
    EXPECT_EQ(dropped.value(QStringLiteral("count")).toInteger(), 120);
    const QJsonObject warning =
        hio::cli::warningEventJson(hio::makeError(hio::ErrorCode::ProbeFirmwareOutdated));
    EXPECT_EQ(warning.value(QStringLiteral("type")).toString(), QStringLiteral("warning"));
    EXPECT_EQ(
        warning.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(),
        QStringLiteral("probe.firmware_outdated"));
}

TEST(ConsoleEvents, HumanTextAndColors) {
    EXPECT_EQ(hio::cli::humanLine(logLine(LogLevel::Error), false), logLine(LogLevel::Error).text);
    EXPECT_EQ(hio::cli::humanLine(logLine(LogLevel::Error), true),
              "\x1b[31m" + logLine(LogLevel::Error).text + "\x1b[0m");
    EXPECT_EQ(hio::cli::humanLine(logLine(LogLevel::Warning), true).substr(0, 5), "\x1b[33m");
    EXPECT_EQ(hio::cli::humanLine(logLine(LogLevel::Debug), true).substr(0, 4), "\x1b[2m");
    EXPECT_EQ(hio::cli::humanLine(logLine(LogLevel::Info), true), logLine(LogLevel::Info).text);
    ConsoleLine shell = logLine(LogLevel::None);
    shell.channel = Channel::Shell;
    EXPECT_EQ(hio::cli::humanLine(shell, true), shell.text);

    EXPECT_EQ(hio::cli::humanConnection(ConnectionEvent{.state = ConnectionState::Connected,
                                                        .error = std::nullopt,
                                                        .targetMillivolts = std::nullopt}),
              "-- connected --");
    EXPECT_EQ(hio::cli::humanConnection(
                  ConnectionEvent{.state = ConnectionState::Disconnected,
                                  .error = hio::makeError(hio::ErrorCode::TargetNoPower),
                                  .targetMillivolts = std::nullopt}),
              "-- disconnected: The device has no power. --");
    EXPECT_EQ(hio::cli::humanWarning(hio::makeError(hio::ErrorCode::ProbeFirmwareOutdated)),
              "Warning: The J-Link firmware is outdated. Run `hio jlink update-firmware`.");
}

} // namespace
