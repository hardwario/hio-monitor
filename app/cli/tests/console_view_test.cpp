// ConsoleView with a real DeviceSession on the fake J-Link library (review R28): the --json
// console emits exactly one terminal "stopped" event.

#include "cli_app.h"
#include "console_ui.h"
#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/device_session.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/device/session_registry.h"
#include "hio/hw/jlink/library_locator.h"
#include "output.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <ios>
#include <memory>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

namespace {

namespace fs = std::filesystem;
using hio::cli::ConsoleView;
using hio::cli::Context;
using hio::cli::Output;
using hio::cli::OutputMode;

bool processEventsUntil(const std::function<bool()>& done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

int stoppedEvents(const std::string& out) {
    int count = 0;
    std::istringstream lines(out);
    for (std::string line; std::getline(lines, line);) {
        const QJsonObject event = QJsonDocument::fromJson(QByteArray::fromStdString(line))
                                      .object()
                                      .value(QStringLiteral("data"))
                                      .toObject();
        count += event.value(QStringLiteral("type")) == QStringLiteral("connection") &&
                         event.value(QStringLiteral("state")) == QStringLiteral("stopped")
                     ? 1
                     : 0;
    }
    return count;
}

class JsonConsoleView : public ::testing::Test {
protected:
    void TearDown() override { qunsetenv("HIO_FAKE_JLINK_SCENARIO"); }

    void useScenario(const char* json) const {
        const QString path = m_dir.filePath(QStringLiteral("scenario.json"));
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(json);
        file.close();
        qputenv("HIO_FAKE_JLINK_SCENARIO", path.toUtf8());
    }

    [[nodiscard]] hio::Result<std::unique_ptr<hio::device::DeviceSession>> open() {
        hio::device::SessionOptions options;
        options.profile = hio::device::ProfileRegistry::find("chester");
        options.targetId = "app";
        options.speedKhz = 2000;
        options.latencyMs = 10;
        options.consoleFile = fs::path();
        return hio::device::DeviceSession::open(
            m_registry, hio::jlink::LibraryLocation{.path = fs::path(HIO_FAKE_JLINK_LIBRARY)},
            std::move(options));
    }

    [[nodiscard]] Context& context() { return m_context; }
    [[nodiscard]] std::string out() const { return m_out.str(); }
    void closeOut() { m_out.setstate(std::ios::badbit); } // like a write to a closed pipe

private:
    QTemporaryDir m_dir;
    hio::device::SessionRegistry m_registry{hio::device::RegistryOptions{
        .instanceRoot = fs::path(m_dir.path().toStdString()) / "instances",
        .lockDirectory = fs::path(m_dir.path().toStdString()) / "locks",
        .consoleDirectory = fs::path(m_dir.path().toStdString()) / "console"}};
    std::ostringstream m_out;
    std::ostringstream m_err;
    Output m_output{OutputMode::Json, m_out, m_err, false};
    Context m_context{.mode = OutputMode::Json, .stop = {}, .output = &m_output, .logFile = {}};
};

// A fatal error: the session's Stopped event is the terminal one; printStopped() adds nothing.
TEST_F(JsonConsoleView, OneStoppedEventAfterAFatalError) {
    useScenario(R"({"probes": [{"serialNumber": 1, "vtref": [[0, 150]]}]})");
    auto session = open();
    ASSERT_TRUE(session) << session.error().message;
    ConsoleView view(context(), **session, false, false);
    ASSERT_TRUE(processEventsUntil([&] { return view.fatalError().has_value(); }));
    (*session)->close();
    view.printStopped();
    EXPECT_EQ(stoppedEvents(out()), 1) << out();
}

// A normal end: the session never reported Stopped; printStopped() emits the one event.
TEST_F(JsonConsoleView, OneStoppedEventAfterANormalEnd) {
    useScenario(R"({"probes": [{"serialNumber": 1}]})");
    auto session = open();
    ASSERT_TRUE(session) << session.error().message;
    ConsoleView view(context(), **session, false, false);
    ASSERT_TRUE(processEventsUntil([&] {
        return (*session)->connection().state == hio::device::ConnectionState::Connected &&
               out().find("\"connected\"") != std::string::npos;
    }));
    (*session)->close();
    view.printStopped();
    view.printStopped(); // idempotent
    EXPECT_EQ(stoppedEvents(out()), 1) << out();
}

// Review R34: the first connection fails with timeout (still connecting): one "stopped" event.
TEST_F(JsonConsoleView, OneStoppedEventAfterAConnectTimeout) {
    useScenario(R"({"probes": [{"serialNumber": 1, "connectDelayMs": 300,
                                "rtt": {"foundAfterMs": 600000}}]})");
    auto session = open();
    ASSERT_TRUE(session) << session.error().message;
    ConsoleView view(context(), **session, false, false);
    const hio::Status status = hio::cli::waitForConsoleConnection(context(), **session, view, false,
                                                                  std::chrono::milliseconds(200));
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, hio::ErrorCode::Timeout);
    EXPECT_EQ(stoppedEvents(out()), 1) << out();
    view.printStopped(); // idempotent
    EXPECT_EQ(stoppedEvents(out()), 1) << out();
}

// Review R34: Ctrl+C while connecting (cancelled), also with --auto-reconnect.
TEST_F(JsonConsoleView, OneStoppedEventWhenCancelledWhileConnecting) {
    useScenario(R"({"probes": [{"serialNumber": 1, "connectDelayMs": 300,
                                "rtt": {"foundAfterMs": 600000}}]})");
    auto session = open();
    ASSERT_TRUE(session) << session.error().message;
    std::stop_source stop;
    Context cancelled = context();
    cancelled.stop = stop.get_token();
    ConsoleView view(cancelled, **session, false, false);
    ASSERT_TRUE(
        processEventsUntil([&] { return out().find("\"connecting\"") != std::string::npos; }));
    stop.request_stop();
    const hio::Status status = hio::cli::waitForConsoleConnection(cancelled, **session, view, true,
                                                                  std::chrono::milliseconds(10000));
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, hio::ErrorCode::Cancelled);
    EXPECT_EQ(stoppedEvents(out()), 1) << out();
}

// Review R32: a failed write to stdout calls the output-closed handler once (the console turns
// it into a stop request).
TEST_F(JsonConsoleView, FailedStdoutWriteCallsTheOutputClosedHandler) {
    useScenario(R"({"probes": [{"serialNumber": 1}]})");
    auto session = open();
    ASSERT_TRUE(session) << session.error().message;
    ConsoleView view(context(), **session, false, false);
    int calls = 0;
    view.setOutputClosedHandler([&calls] { ++calls; });
    closeOut();
    ASSERT_TRUE(processEventsUntil([&] { return view.outputClosed(); }));
    ASSERT_TRUE(processEventsUntil(
        [&] { return (*session)->connection().state == hio::device::ConnectionState::Connected; }));
    (*session)->close();
    view.printStopped();
    EXPECT_EQ(calls, 1);
}

} // namespace
