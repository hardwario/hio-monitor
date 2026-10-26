// Integration through the real locator, loader and library copies against the test-only fake
// J-Link library (plan/features/0002, phase E2): two sessions at once, probe status with locks,
// probe tools, stale instance cleanup, non-blocking open.

#include "hio/core/error.h"
#include "hio/device/console_line.h"
#include "hio/device/device_session.h"
#include "hio/device/operation.h"
#include "hio/device/probe_lock.h"
#include "hio/device/probe_tools.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/device/session_registry.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/program_progress.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/programmer.h"
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

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
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

// ADR-0176 (plan 0021 review): output already waiting in the target's buffer when a line is sent
// comes before the line's echo, although the next read cycle is far away (1 s latency).
TEST_F(FakeLibrarySession, BufferedOutputComesBeforeTheEcho) {
    useScenario(R"({"probes": [{"serialNumber": 7,
        "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "first\n"},
                           {"atMs": 250, "buffer": 0, "text": "pending\n"}]}}]})");
    auto opts = options(7);
    opts.latencyMs = hio::device::maxLatencyMs;
    auto session = DeviceSession::open(registry(), location(), std::move(opts));
    ASSERT_TRUE(session) << session.error().message;
    Recorder recorder(**session);
    ASSERT_TRUE(hio::device::waitUntilConnected(**session, std::chrono::seconds(10)));
    ASSERT_TRUE(waitFor([&] { return !recorder.texts(Channel::Shell, Direction::Out).empty(); }));
    hio::device::test::spin(std::chrono::milliseconds(450));
    ASSERT_TRUE((*session)->sendLine("version"));
    ASSERT_TRUE(waitFor([&] { return !recorder.texts(Channel::Shell, Direction::In).empty(); }));
    std::vector<std::string> shell;
    for (const auto& line : recorder.lines) {
        if (line.channel == Channel::Shell) {
            shell.push_back(line.text);
        }
    }
    EXPECT_EQ(shell, (std::vector<std::string>{"first", "pending", "version"}));
}

// ADR-0176: when that read fails, the link is reported down and the line is not written.
TEST_F(FakeLibrarySession, ALineIsNotWrittenWhenTheReadBeforeItFails) {
    useScenario(R"({"probes": [{"serialNumber": 7,
        "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "first\n"}],
                "readErrors": [[300, 600000]]}}]})");
    auto opts = options(7);
    opts.latencyMs = hio::device::maxLatencyMs;
    auto session = DeviceSession::open(registry(), location(), std::move(opts));
    ASSERT_TRUE(session) << session.error().message;
    Recorder recorder(**session);
    ASSERT_TRUE(hio::device::waitUntilConnected(**session, std::chrono::seconds(10)));
    ASSERT_TRUE(waitFor([&] { return !recorder.texts(Channel::Shell, Direction::Out).empty(); }));
    hio::device::test::spin(std::chrono::milliseconds(450));
    ASSERT_TRUE((*session)->sendLine("version"));
    ASSERT_TRUE(waitFor([&] {
        return std::ranges::any_of(recorder.connections, [](const auto& event) {
            return event.state == ConnectionState::Disconnected;
        });
    }));
    hio::device::test::spin(std::chrono::milliseconds(100));
    EXPECT_TRUE(recorder.texts(Channel::Shell, Direction::In).empty());
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
    ASSERT_TRUE((*session)->reset("app", hio::device::ResetMode::Go));
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

// ---- M2 flashing through the real loader and the fake flash model (plan/features/0003, E2) ----

std::shared_ptr<const hio::firmware::FirmwareImage> testImage(std::size_t size = 8192) {
    std::vector<hio::firmware::Segment> segments;
    segments.push_back(hio::firmware::Segment{
        .address = 0x2000, .data = std::vector<std::byte>(size, std::byte{0x3C})});
    return std::make_shared<const hio::firmware::FirmwareImage>(
        *hio::firmware::FirmwareImage::fromSegments(std::move(segments)));
}

hio::device::OperationResult runToEnd(hio::device::DeviceSession& session,
                                      const hio::Result<hio::device::OperationId>& id) {
    EXPECT_TRUE(id) << (id ? std::string() : id.error().message);
    if (!id) {
        return {};
    }
    return hio::device::waitForOperation(session, *id);
}

TEST_F(FakeLibrarySession, FlashWithRttAttachedPausesAndReattaches) {
    auto session = DeviceSession::open(registry(), location(), options(801000001));
    ASSERT_TRUE(session);
    Recorder events(**session);
    ASSERT_TRUE(hio::device::waitUntilConnected(**session, std::chrono::milliseconds(10000)));
    ASSERT_TRUE(waitFor([&] { return !events.lines.empty(); }));
    const std::size_t linesBefore = events.lines.size();
    const auto result = runToEnd(**session, (*session)->flash("app", testImage(), {}));
    ASSERT_TRUE(result.status) << result.status.error().message << " "
                               << result.status.error().detail;
    ASSERT_TRUE(result.chip.has_value());
    EXPECT_EQ(result.chip.value_or(hio::jlink::ChipInfo{}).name, "nRF52840");
    EXPECT_EQ(events.phases(1),
              std::vector<hio::firmware::ProgramPhase>(
                  hio::device::plannedPhases(hio::device::OperationKind::Flash, false).begin(),
                  hio::device::plannedPhases(hio::device::OperationKind::Flash, false).end()));
    // The reset after programming restarts the fake target: its boot output is read again.
    ASSERT_TRUE(waitFor([&] { return events.lines.size() > linesBefore; }));
    EXPECT_EQ(events.count(ConnectionState::Disconnected), 0);
    // Read back through an operation.
    const auto read = runToEnd(**session, (*session)->readMemory("app", 0x2000, 4));
    ASSERT_TRUE(read.status);
    EXPECT_EQ(read.data, std::vector<std::byte>(4, std::byte{0x3C}));
}

TEST_F(FakeLibrarySession, TwoSessionsFlashConcurrently) {
    auto a = DeviceSession::open(registry(), location(), options(801000001));
    auto b = DeviceSession::open(registry(), location(), options(801000002));
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    ASSERT_TRUE(hio::device::waitUntilConnected(**a, std::chrono::milliseconds(10000)));
    ASSERT_TRUE(hio::device::waitUntilConnected(**b, std::chrono::milliseconds(10000)));
    const auto idA = (*a)->flash("app", testImage(64ULL * 1024), {});
    const auto idB = (*b)->flash("app", testImage(64ULL * 1024), {});
    ASSERT_TRUE(idA);
    ASSERT_TRUE(idB);
    const auto resultA = hio::device::waitForOperation(**a, *idA);
    const auto resultB = hio::device::waitForOperation(**b, *idB);
    EXPECT_TRUE(resultA.status);
    EXPECT_TRUE(resultB.status);
}

TEST_F(FakeLibrarySession, VerifyMismatchIsReported) {
    useScenario(R"({"probes": [{"serialNumber": 5, "chip": {"verifyMismatchAt": 8200}}]})");
    SessionOptions opts = options(5U);
    opts.attachRtt = false;
    auto session = DeviceSession::open(registry(), location(), opts);
    ASSERT_TRUE(session);
    const auto result = runToEnd(**session, (*session)->flash("app", testImage(), {}));
    ASSERT_FALSE(result.status);
    EXPECT_EQ(result.status.error().code, ErrorCode::FirmwareVerifyFailed);
    EXPECT_EQ(result.status.error().message, "Verification failed at address 0x00002008.");
}

TEST_F(FakeLibrarySession, ProtectedChipNeedsRecover) {
    useScenario(R"({"probes": [{"serialNumber": 6, "chip": {"protected": true, "eraseAllPolls": 3,
        "memory": [{"address": 268439680, "hex": "babecafe"}]}}]})");
    SessionOptions opts = options(6U);
    opts.attachRtt = false;
    auto session = DeviceSession::open(registry(), location(), opts);
    ASSERT_TRUE(session);
    const auto flash = runToEnd(**session, (*session)->flash("app", testImage(), {}));
    ASSERT_FALSE(flash.status);
    EXPECT_EQ(flash.status.error().code, ErrorCode::TargetProtected);
    const auto recover = runToEnd(**session, (*session)->recover("app"));
    ASSERT_TRUE(recover.status) << recover.status.error().detail;
    // ERASEALL also erased the UICR (the PIB is gone).
    const auto pib = runToEnd(**session, (*session)->readMemory("app", 0x10001080, 4));
    ASSERT_TRUE(pib.status);
    EXPECT_EQ(pib.data, std::vector<std::byte>(4, std::byte{0xFF}));
    EXPECT_TRUE(runToEnd(**session, (*session)->flash("app", testImage(), {})).status);
}

TEST_F(FakeLibrarySession, WriteFaultIsAProgramFailure) {
    useScenario(R"({"probes": [{"serialNumber": 7, "chip": {"writeFault": true}}]})");
    SessionOptions opts = options(7U);
    opts.attachRtt = false;
    auto session = DeviceSession::open(registry(), location(), opts);
    ASSERT_TRUE(session);
    const auto result = runToEnd(**session, (*session)->flash("app", testImage(), {}));
    ASSERT_FALSE(result.status);
    EXPECT_EQ(result.status.error().code, ErrorCode::TargetProgramFailed);
    EXPECT_NE(result.status.error().detail.find("simulated write fault"), std::string::npos)
        << result.status.error().detail;
}

TEST_F(FakeLibrarySession, LteEraseAllAndRecoverWithTheUnlockImage) {
    useScenario(R"({"probes": [{"serialNumber": 8, "chip": {"family": "nrf91",
        "memory": [{"address": 16744712, "hex": "babecafe"}]}}]})");
    SessionOptions opts = options(8U);
    opts.targetId = "lte";
    opts.attachRtt = false;
    auto session = DeviceSession::open(registry(), location(), opts);
    ASSERT_TRUE(session) << session.error().message;
    const auto erase = runToEnd(**session, (*session)->erase("lte", hio::device::EraseScope::All));
    ASSERT_TRUE(erase.status) << erase.status.error().detail;
    EXPECT_EQ(erase.chip.value_or(hio::jlink::ChipInfo{}).name, "nRF9151");
    const auto recover = runToEnd(**session, (*session)->recover("lte"));
    ASSERT_TRUE(recover.status) << recover.status.error().detail;
    const auto code = runToEnd(**session, (*session)->readMemory("lte", 0x0, 4));
    ASSERT_TRUE(code.status);
    EXPECT_NE(code.data, std::vector<std::byte>(4, std::byte{0xFF})); // the image is there
}

// Review R1: a protected chip is never mass-erased by connecting: flashing it fails with
// target.protected again and again (without the hook the first connect would erase it).
TEST_F(FakeLibrarySession, ProtectedChipIsNotErasedByConnecting) {
    useScenario(R"({"probes": [{"serialNumber": 6, "chip": {"protected": true,
        "memory": [{"address": 268439680, "hex": "babecafe"}]}}]})");
    SessionOptions opts = options(6U);
    opts.attachRtt = false;
    auto session = DeviceSession::open(registry(), location(), opts);
    ASSERT_TRUE(session);
    for (int i = 0; i < 2; ++i) {
        const auto flash = runToEnd(**session, (*session)->flash("app", testImage(), {}));
        ASSERT_FALSE(flash.status);
        EXPECT_EQ(flash.status.error().code, ErrorCode::TargetProtected);
    }
}

// Review R15: on a real DAP an absent AP reads 0. The probe sits on the LTE MCU (protected, so
// FICR is unreadable) while the session is for app: AP 1 reads 0, AP 4 holds a Nordic CTRL-AP ->
// target.wrong_family, not "protected".
TEST_F(FakeLibrarySession, AbsentApsReadingZeroAreTheWrongConnector) {
    useScenario(R"({"probes": [{"serialNumber": 9, "chip": {"family": "nrf91", "protected": true,
        "absentApsReadZero": true}}]})");
    SessionOptions opts = options(9U);
    opts.attachRtt = false;
    auto session = DeviceSession::open(registry(), location(), opts);
    ASSERT_TRUE(session);
    const auto flash = runToEnd(**session, (*session)->flash("app", testImage(), {}));
    ASSERT_FALSE(flash.status);
    EXPECT_EQ(flash.status.error().code, ErrorCode::TargetWrongFamily);
    const auto recover = runToEnd(**session, (*session)->recover("app"));
    ASSERT_FALSE(recover.status);
    EXPECT_EQ(recover.status.error().code, ErrorCode::TargetWrongFamily);
}

// ADR-0091: the scenario's VTref reaches the Connected event.
TEST_F(FakeLibrarySession, ConnectedEventCarriesTheScenarioVoltage) {
    useScenario(R"({"probes": [{"serialNumber": 11, "vtref": [[0, 3712]]}]})");
    auto session = DeviceSession::open(registry(), location(), options(11U));
    ASSERT_TRUE(session);
    Recorder events(**session);
    ASSERT_TRUE(hio::device::waitUntilConnected(**session, std::chrono::milliseconds(10000)));
    ASSERT_TRUE(waitFor([&] { return events.reached(ConnectionState::Connected); }));
    EXPECT_EQ(events.connections.back().targetMillivolts, 3712);
    EXPECT_EQ((*session)->connection().targetMillivolts, 3712);
}

} // namespace

namespace {

using hio::device::AccessWidth;

// M6 (plan/features/0013): the target operations through a real copy of the fake library. The
// console stays Connected across halt / go and memory accesses (ADR-0157).
TEST_F(FakeLibrarySession, TargetOperationsThroughTheLibrary) {
    useScenario(R"json({"probes": [{"serialNumber": 801000001,
        "rtt": {"output": [{"atMs": 0, "buffer": 0, "text": "booted\n"},
                           {"atMs": 400, "buffer": 0, "text": "later\n"}]},
        "chip": {"ram": [{"address": 536870912, "size": 262144}],
                 "registers": [{"name": "R0", "value": 5}, {"name": "PC", "value": 8192}],
                 "zones": [{"name": "AHB-AP", "description": "AHB-AP (AP0)", "virtualAddress": 0}]}}]})json");
    auto session = DeviceSession::open(registry(), location(), options(801000001));
    ASSERT_TRUE(session) << session.error().message;
    Recorder events(**session);
    ASSERT_TRUE(hio::device::waitUntilConnected(**session, std::chrono::milliseconds(10000)));
    const auto halted = runToEnd(**session, (*session)->halt("app"));
    ASSERT_TRUE(halted.status) << halted.status.error().detail;
    const auto status = runToEnd(**session, (*session)->targetStatus("app"));
    ASSERT_TRUE(status.status) << status.status.error().detail;
    EXPECT_TRUE(status.targetStatus.value_or(hio::device::TargetStatus{}).halted);
    EXPECT_EQ(status.targetStatus.value_or(hio::device::TargetStatus{}).coreId, 0x2BA01477U);
    const auto registers = runToEnd(**session, (*session)->readRegisters("app"));
    ASSERT_TRUE(registers.status) << registers.status.error().detail;
    ASSERT_EQ(registers.registers.size(), 2U);
    EXPECT_EQ(registers.registers[0].value, 5U);
    const std::vector<std::byte> data{std::byte{0xEF}, std::byte{0xBE}, std::byte{0xAD},
                                      std::byte{0xDE}};
    ASSERT_TRUE(
        runToEnd(**session, (*session)->writeMemory("app", 0x20000100, data, AccessWidth::Bits32))
            .status);
    const auto read =
        runToEnd(**session, (*session)->readMemory("app", 0x20000100, 4, AccessWidth::Bits16));
    ASSERT_TRUE(read.status) << read.status.error().detail;
    EXPECT_EQ(read.data, data);
    const auto zones = runToEnd(**session, (*session)->memoryZones("app"));
    ASSERT_TRUE(zones.status);
    ASSERT_EQ(zones.zones.size(), 1U);
    EXPECT_EQ(zones.zones[0].description, "AHB-AP (AP0)");
    ASSERT_TRUE(runToEnd(**session, (*session)->go("app")).status);
    // RTT kept running across all of it.
    ASSERT_TRUE(waitFor([&] {
        const auto lines = events.texts(Channel::Shell, Direction::Out);
        return std::ranges::find(lines, "later") != lines.end();
    }));
    EXPECT_EQ(events.count(ConnectionState::Disconnected), 0);
    EXPECT_EQ((*session)->connection().state, ConnectionState::Connected);
}

// ADR-0151: a J-Link library without the optional exports: RTT works, the target operations that
// need them fail with dependency.jlink_function_missing.
TEST_F(FakeLibrarySession, MissingOptionalExport) {
    auto session = DeviceSession::open(
        registry(), hio::jlink::LibraryLocation{.path = fs::path(HIO_FAKE_JLINK_BASIC_LIBRARY)},
        options(801000001));
    ASSERT_TRUE(session) << session.error().message;
    ASSERT_TRUE(hio::device::waitUntilConnected(**session, std::chrono::milliseconds(10000)));
    const auto status = runToEnd(**session, (*session)->targetStatus("app"));
    ASSERT_FALSE(status.status);
    EXPECT_EQ(status.status.error().code, ErrorCode::DependencyJLinkFunctionMissing);
    EXPECT_EQ(status.status.error().detail, "JLINKARM_IsHalted");
    const auto zones = runToEnd(**session, (*session)->memoryZones("app"));
    EXPECT_EQ(zones.status.error().detail, "JLINK_GetMemZones");
    EXPECT_TRUE(runToEnd(**session, (*session)->halt("app")).status);
}

// Review Q10: the scenario key chip.halted starts the CPU halted, so registers can be read without
// a halt first; go lets it run.
TEST_F(FakeLibrarySession, ScenarioStartsTheCpuHalted) {
    useScenario(R"json({"probes": [{"serialNumber": 801000001,
        "chip": {"halted": true,
                 "registers": [{"name": "R0", "value": 7}, {"name": "PC", "value": 4096}]}}]})json");
    auto session = DeviceSession::open(registry(), location(), options(801000001));
    ASSERT_TRUE(session) << session.error().message;
    ASSERT_TRUE(hio::device::waitUntilConnected(**session, std::chrono::milliseconds(10000)));
    const auto status = runToEnd(**session, (*session)->targetStatus("app"));
    ASSERT_TRUE(status.status) << status.status.error().detail;
    EXPECT_TRUE(status.targetStatus.value_or(hio::device::TargetStatus{}).halted);
    const auto registers = runToEnd(**session, (*session)->readRegisters("app"));
    ASSERT_TRUE(registers.status) << registers.status.error().detail;
    ASSERT_EQ(registers.registers.size(), 2U);
    EXPECT_EQ(registers.registers[0].value, 7U);
    ASSERT_TRUE(runToEnd(**session, (*session)->go("app")).status);
    const auto running = runToEnd(**session, (*session)->targetStatus("app"));
    ASSERT_TRUE(running.status);
    EXPECT_FALSE(running.targetStatus.value_or(hio::device::TargetStatus{}).halted);
    EXPECT_EQ(runToEnd(**session, (*session)->readRegisters("app")).status.error().code,
              ErrorCode::TargetNotHalted);
}

// ADR-0160 (review S1): reads are not limited to the profile ranges: FICR reads work, an unmapped
// address is target.memory_access_failed from the library.
TEST_F(FakeLibrarySession, FicrIsReadableAndUnmappedReadsFail) {
    useScenario(R"({"probes": [{"serialNumber": 801000001}]})");
    auto session = DeviceSession::open(registry(), location(), options(801000001));
    ASSERT_TRUE(session) << session.error().message;
    ASSERT_TRUE(hio::device::waitUntilConnected(**session, std::chrono::milliseconds(10000)));
    const std::vector<std::byte> part{std::byte{0x40}, std::byte{0x28}, std::byte{0x05},
                                      std::byte{0x00}}; // INFO.PART 0x52840, little-endian
    const auto bytes = runToEnd(**session, (*session)->readMemory("app", 0x10000100, 4));
    ASSERT_TRUE(bytes.status) << bytes.status.error().detail;
    EXPECT_EQ(bytes.data, part);
    const auto words =
        runToEnd(**session, (*session)->readMemory("app", 0x10000100, 4, AccessWidth::Bits32));
    ASSERT_TRUE(words.status) << words.status.error().detail;
    EXPECT_EQ(words.data, part);
    const auto unmapped = runToEnd(**session, (*session)->readMemory("app", 0x30000000, 4));
    ASSERT_FALSE(unmapped.status);
    EXPECT_EQ(unmapped.status.error().code, ErrorCode::TargetMemoryAccessFailed);
    EXPECT_EQ((*session)->connection().state, ConnectionState::Connected);
}

// The RTT control block address reaches the library: with "autoSearch" false the fake finds the
// block only when RTT START carries the scenario's address.
TEST_F(FakeLibrarySession, RttAddressIsPassedToTheLibrary) {
    useScenario(R"json({"probes": [{"serialNumber": 801000001,
        "rtt": {"blockAddress": 536875008, "autoSearch": false,
                "output": [{"atMs": 0, "buffer": 0, "text": "found\n"}]}}]})json");
    SessionOptions opts = options(801000001);
    opts.rttAddress = 0x20001000U;
    auto session = DeviceSession::open(registry(), location(), opts);
    ASSERT_TRUE(session) << session.error().message;
    Recorder events(**session);
    ASSERT_TRUE(hio::device::waitUntilConnected(**session, std::chrono::milliseconds(10000)));
    ASSERT_TRUE(waitFor([&] { return !events.lines.empty(); }));
    EXPECT_EQ(events.lines.front().text, "found");
}

} // namespace
