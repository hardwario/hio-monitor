// Integration through the real locator, loader and library copies against the test-only fake
// J-Link library (plan/features/0002, phase E2): two sessions at once, probe status with locks,
// probe tools, stale instance cleanup, non-blocking open.

#include "hio/core/error.h"
#include "hio/device/console_line.h"
#include "hio/device/device_session.h"
#include "hio/device/probe_lock.h"
#include "hio/device/probe_tools.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/device/session_registry.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/types.h"
#include "session_test_support.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hio::ErrorCode;
using hio::device::Channel;
using hio::device::ConnectionState;
using hio::device::DeviceSession;
using hio::device::Direction;
using hio::device::SessionOptions;
using hio::device::SessionRegistry;
using hio::device::test::Recorder;
using hio::device::test::waitFor;

constexpr const char* twoProbes = R"({
  "probes": [
    {"serialNumber": 801000001, "product": "Test Probe",
     "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted A\n"},
                        {"atMs": 0, "buffer": 1, "text": "[00:00:00.001,000] <inf> app: A\n"}],
             "responses": {"version": "Firmware version: A\n"}}},
    {"serialNumber": 801000002, "product": "Test Probe 2",
     "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "uart:~$ booted B\n"}],
             "responses": {"version": "Firmware version: B\n"}}}
  ]})";

class FakeLibrarySession : public ::testing::Test {
protected:
    void SetUp() override {
        useScenario(twoProbes);
        resetRegistry();
    }
    void TearDown() override { qunsetenv("HIO_FAKE_JLINK_SCENARIO"); }

    void useScenario(const char* json) const {
        const QString path = m_dir.filePath(QStringLiteral("scenario.json"));
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(json);
        file.close();
        qputenv("HIO_FAKE_JLINK_SCENARIO", path.toUtf8());
    }

    void resetRegistry() {
        m_registry = std::make_unique<SessionRegistry>(
            hio::device::RegistryOptions{.instanceRoot = root() / "instances",
                                         .lockDirectory = root() / "locks",
                                         .consoleDirectory = root() / "console"});
    }

    [[nodiscard]] fs::path root() const { return {m_dir.path().toStdString()}; }
    [[nodiscard]] SessionRegistry& registry() { return *m_registry; }

    [[nodiscard]] static hio::jlink::LibraryLocation location() {
        return hio::jlink::LibraryLocation{.path = fs::path(HIO_FAKE_JLINK_LIBRARY)};
    }

    [[nodiscard]] static SessionOptions options(std::optional<std::uint32_t> sn) {
        SessionOptions options;
        options.profile = hio::device::ProfileRegistry::find("chester");
        options.targetId = "app";
        options.probeSn = sn;
        options.speedKhz = 2000;
        options.latencyMs = 10;
        options.consoleFile = fs::path();
        return options;
    }

private:
    QTemporaryDir m_dir;
    std::unique_ptr<SessionRegistry> m_registry;
};

TEST_F(FakeLibrarySession, TwoSessionsAtOnceThroughLibraryCopies) {
    auto a = DeviceSession::open(registry(), location(), options(801000001));
    auto b = DeviceSession::open(registry(), location(), options(801000002));
    ASSERT_TRUE(a) << a.error().message;
    ASSERT_TRUE(b) << b.error().message;
    Recorder ra(**a);
    Recorder rb(**b);
    ASSERT_TRUE(hio::device::waitUntilConnected(**a, std::chrono::milliseconds(10000)));
    ASSERT_TRUE(hio::device::waitUntilConnected(**b, std::chrono::milliseconds(10000)));
    ASSERT_TRUE(waitFor([&] { return ra.lines.size() >= 2 && !rb.lines.empty(); }));
    EXPECT_EQ(ra.texts(Channel::Shell, Direction::Out).front(), "uart:~$ booted A");
    EXPECT_EQ(ra.texts(Channel::Log, Direction::Out).front(), "[00:00:00.001,000] <inf> app: A");
    EXPECT_EQ(rb.texts(Channel::Shell, Direction::Out).front(), "uart:~$ booted B");

    ASSERT_TRUE((*a)->sendLine("version"));
    ASSERT_TRUE((*b)->sendLine("version"));
    ASSERT_TRUE(waitFor([&] {
        const auto outA = ra.texts(Channel::Shell, Direction::Out);
        const auto outB = rb.texts(Channel::Shell, Direction::Out);
        return outA.size() >= 2 && outB.size() >= 2;
    }));
    EXPECT_EQ(ra.texts(Channel::Shell, Direction::Out)[1], "Firmware version: A");
    EXPECT_EQ(rb.texts(Channel::Shell, Direction::Out)[1], "Firmware version: B");
    EXPECT_EQ((*a)->probe().product, "Test Probe");

    // Two library copies are loaded at the same time, and removed again when the sessions end.
    std::size_t copies = 0;
    std::error_code ec;
    for (fs::directory_iterator it(root() / "instances", ec), end; !ec && it != end;
         it.increment(ec)) {
        ++copies;
    }
    EXPECT_EQ(copies, 2U);
    (*a)->close();
    (*b)->close();
    copies = 0;
    for (fs::directory_iterator it(root() / "instances", ec), end; !ec && it != end;
         it.increment(ec)) {
        ++copies;
    }
    EXPECT_EQ(copies, 0U);
}

TEST_F(FakeLibrarySession, ProbeStatusShowsSessionsAndOtherProcesses) {
    auto session = DeviceSession::open(registry(), location(), options(801000001));
    ASSERT_TRUE(session);
    ASSERT_TRUE(hio::device::waitUntilConnected(**session, std::chrono::milliseconds(10000)));
    const auto list = registry().enumerateProbes(location());
    ASSERT_TRUE(list) << list.error().detail;
    EXPECT_EQ(list->libraryVersion, 81000U);
    ASSERT_EQ(list->probes.size(), 2U);
    EXPECT_EQ(list->probes[0].usedBySession, session->get());
    EXPECT_FALSE(list->probes[0].usedByOtherProcess.has_value());
    EXPECT_EQ(list->probes[1].usedBySession, nullptr);
    EXPECT_FALSE(list->probes[1].usedByOtherProcess.has_value());
}

TEST_F(FakeLibrarySession, ProbeToolsInspectAndUpdateFirmware) {
    useScenario(R"({"probes": [{"serialNumber": 5,
        "firmware": "J-Link V11 compiled Sep 21 2023 10:38:43",
        "libraryFirmware": "J-Link V11 compiled Oct  1 2024 09:00:00",
        "vtref": [[0, 3299]]}]})");
    const auto details = hio::device::inspectProbe(registry(), location(), std::nullopt);
    ASSERT_TRUE(details) << details.error().detail;
    EXPECT_EQ(details->probe.serialNumber, 5U);
    EXPECT_EQ(details->libraryVersion, 81000U);
    EXPECT_EQ(details->firmwareStatus, hio::jlink::FirmwareStatus::Outdated);
    EXPECT_EQ(details->libraryFirmware, "J-Link V11 compiled Oct  1 2024 09:00:00");
    EXPECT_EQ(details->targetMillivolts, 3299);
    EXPECT_FALSE(hio::device::ProbeLock::owner(5, root() / "locks").has_value()); // released

    const auto update = hio::device::updateProbeFirmware(registry(), location(), 5U);
    ASSERT_TRUE(update) << update.error().detail;
    EXPECT_EQ(update->before, hio::jlink::FirmwareStatus::Outdated);

    const auto missing = hio::device::inspectProbe(registry(), location(), 6U);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, ErrorCode::ProbeNotFound);
}

TEST_F(FakeLibrarySession, OpenDoesNotWaitForASlowConnect) {
    useScenario(R"({"probes": [{"serialNumber": 9, "connectDelayMs": 1000}]})");
    const auto start = std::chrono::steady_clock::now();
    auto session = DeviceSession::open(registry(), location(), options(std::nullopt));
    // Half of the 1000 ms connect it must not wait for (review R46).
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(500));
    ASSERT_TRUE(session);
    ASSERT_TRUE(hio::device::waitUntilConnected(**session, std::chrono::milliseconds(10000)));
    EXPECT_EQ((*session)->probe().serialNumber, 9U);
}

TEST_F(FakeLibrarySession, ResetRestartsTheTargetAndReattaches) {
    useScenario(R"({"probes": [{"serialNumber": 3,
        "rtt": {"foundAfterMs": 50, "staleUntilMs": 100,
                "output": [{"atMs": 120, "buffer": 0, "text": "boot\n"}]}}]})");
    auto session = DeviceSession::open(registry(), location(), options(3U));
    ASSERT_TRUE(session);
    Recorder events(**session);
    ASSERT_TRUE(hio::device::waitUntilConnected(**session, std::chrono::milliseconds(10000)));
    ASSERT_TRUE(waitFor([&] { return events.lines.size() == 1; }));
    ASSERT_TRUE((*session)->reset(false));
    ASSERT_TRUE(waitFor([&] { return events.lines.size() == 2; })); // replayed after the reset
    EXPECT_EQ(events.lines[1].text, "boot");
    EXPECT_EQ(events.count(ConnectionState::Disconnected), 0);
}

TEST_F(FakeLibrarySession, NoPowerFromTheVoltageTimeline) {
    // The first power check is 1 s after attaching; the drop must come after a healthy reading.
    useScenario(R"({"probes": [{"serialNumber": 4, "vtref": [[0, 3300], [1800, 0]]}]})");
    auto session = DeviceSession::open(registry(), location(), options(4U));
    ASSERT_TRUE(session);
    Recorder events(**session);
    ASSERT_TRUE(hio::device::waitUntilConnected(**session, std::chrono::milliseconds(10000)));
    ASSERT_TRUE(waitFor([&] { return events.reached(ConnectionState::Disconnected); }));
    EXPECT_EQ(events.connections.back().error.value_or(hio::Error{}).code,
              ErrorCode::TargetNoPower);
}

TEST_F(FakeLibrarySession, StaleInstancesAreRemovedAtStartup) {
    const fs::path stale = root() / "instances" / "instance-crashed";
    fs::create_directories(stale);
    fs::last_write_time(stale, fs::file_time_type::clock::now() - std::chrono::hours(48));
    resetRegistry();
    EXPECT_FALSE(fs::exists(stale));
}

// Review R12: sessions use USB probes only, also when a network J-Link is listed first.
TEST_F(FakeLibrarySession, NetworkProbesAreNotUsedForSessions) {
    useScenario(R"({"probes": [
        {"serialNumber": 70, "product": "Test Probe IP", "connection": "ip"},
        {"serialNumber": 71, "product": "Test Probe"}]})");
    auto session = DeviceSession::open(registry(), location(), options(std::nullopt));
    ASSERT_TRUE(session);
    ASSERT_TRUE(hio::device::waitUntilConnected(**session, std::chrono::milliseconds(10000)));
    EXPECT_EQ((*session)->probe().serialNumber, 71U);
    (*session)->close();

    auto network = DeviceSession::open(registry(), location(), options(70U));
    ASSERT_TRUE(network);
    const auto status =
        hio::device::waitUntilConnected(**network, std::chrono::milliseconds(10000));
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::ProbeNotFound);
    EXPECT_NE(status.error().message.find("network J-Link"), std::string::npos);

    const auto list = registry().enumerateProbes(location()); // `jlink list` shows both
    ASSERT_TRUE(list);
    EXPECT_EQ(list->probes.size(), 2U);
}

} // namespace
