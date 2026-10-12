// DeviceSession with an in-process scripted J-Link (plan/features/0002, phase D1). Run under
// ASan/UBSan and TSan by the sanitizer presets.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/device_session.h"
#include "hio/device/operation.h"
#include "hio/device/probe_lock.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/device/session_registry.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/types.h"
#include "scripted_api.h"
#include "session_internal.h"
#include "session_test_support.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>
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
using hio::device::detail::OpenHooks;
using hio::device::test::Recorder;
using hio::device::test::ScriptedApi;
using hio::device::test::ScriptedTarget;
using hio::device::test::spin;
using hio::device::test::waitFor;

constexpr std::uint32_t defaultSerial = 801000001;

class Session : public ::testing::Test {
protected:
    Session()
        : m_registry(std::make_unique<SessionRegistry>(
              hio::device::RegistryOptions{.instanceRoot = root() / "instances",
                                           .lockDirectory = root() / "locks",
                                           .consoleDirectory = root() / "console"})) {}

    [[nodiscard]] fs::path root() const { return {m_dir.path().toStdString()}; }

    [[nodiscard]] static SessionOptions options() {
        SessionOptions options;
        options.profile = hio::device::ProfileRegistry::find("chester");
        options.targetId = "app";
        options.speedKhz = 2000;
        options.latencyMs = 5;
        options.consoleFile = fs::path(); // no console file unless a test wants one
        return options;
    }

    // Short timings so that the ADR-0034 rules run in milliseconds.
    [[nodiscard]] OpenHooks hooks(std::shared_ptr<ScriptedTarget> target = nullptr) {
        if (!target) {
            target = m_target;
        }
        OpenHooks hooks;
        hooks.apiFactory = [target, this]() -> hio::Result<std::unique_ptr<hio::jlink::IJLinkApi>> {
            ++m_loads;
            std::this_thread::sleep_for(m_loadDelay.load());
            if (m_loadError) {
                return std::unexpected(hio::makeError(ErrorCode::DependencyJLinkNotFound));
            }
            return std::make_unique<ScriptedApi>(target);
        };
        hooks.timing.powerCheckInterval = std::chrono::milliseconds(20);
        hooks.timing.reconnectInterval = m_reconnectInterval;
        hooks.timing.probe.connectRetryDelay = m_connectRetryDelay;
        // Long enough not to expire under sanitizers; tests of the deadline shorten it (R19).
        hooks.timing.rtt.attachDeadline = m_attachDeadline;
        hooks.timing.rtt.pollInterval = std::chrono::milliseconds(5);
        hooks.timing.rtt.loggerWait = m_loggerWait;
        hooks.timing.rtt.staleRetryDelay = std::chrono::milliseconds(5);
        hooks.timing.rtt.writeTimeout = std::chrono::milliseconds(100);
        hooks.cycles = m_cycles;
        return hooks;
    }

    hio::Result<std::unique_ptr<DeviceSession>> open(SessionOptions opts) {
        return hio::device::detail::openSession(*m_registry, hio::jlink::LibraryLocation{},
                                                std::move(opts), hooks());
    }

    std::unique_ptr<DeviceSession> openConnected(SessionOptions opts, Recorder*& recorder) {
        auto session = open(std::move(opts));
        EXPECT_TRUE(session) << session.error().message;
        if (!session) {
            return nullptr;
        }
        m_recorders.push_back(std::make_unique<Recorder>(**session));
        recorder = m_recorders.back().get();
        EXPECT_TRUE(waitFor([&] { return recorder->reached(ConnectionState::Connected); }))
            << (recorder->connections.empty() || !recorder->connections.back().error
                    ? std::string("no error")
                    : recorder->connections.back().error.value_or(hio::Error{}).detail);
        return std::move(*session);
    }

    [[nodiscard]] ScriptedTarget& target() { return *m_target; }
    [[nodiscard]] SessionRegistry& registry() { return *m_registry; }
    void destroyRegistry() { m_registry = nullptr; }
    [[nodiscard]] int loadCount() const { return m_loads.load(); }
    void setLoadDelay(std::chrono::milliseconds delay) { m_loadDelay = delay; }
    void setLoadError(bool fail) { m_loadError = fail; }
    void useShortAttachDeadline() { m_attachDeadline = std::chrono::milliseconds(400); }
    void setAttachDeadline(std::chrono::milliseconds deadline) { m_attachDeadline = deadline; }
    void setConnectRetryDelay(std::chrono::milliseconds delay) { m_connectRetryDelay = delay; }
    void setLoggerWait(std::chrono::milliseconds wait) { m_loggerWait = wait; }
    void setReconnectInterval(std::chrono::milliseconds interval) {
        m_reconnectInterval = interval;
    }
    [[nodiscard]] std::chrono::milliseconds reconnectInterval() const {
        return m_reconnectInterval;
    }
    // Worker loop iterations of the sessions opened by this test (review R40).
    [[nodiscard]] std::uint64_t cycles() const { return m_cycles->load(); }

    // Runs the event loop until the target counter `field` grew by `delta` (an observable
    // condition instead of a fixed sleep, review R19).
    bool waitForCount(int ScriptedTarget::* field, int delta) {
        const auto read = [field](ScriptedTarget& t) { return t.*field; };
        const int start = target().get(read);
        return waitFor([&] { return target().get(read) >= start + delta; });
    }
    // Waits (without draining) until the terminal output was read and the worker started later
    // read cycles, i.e. every line of that read is in the event queue.
    bool waitUntilOutputEmitted() {
        const auto reads = [](ScriptedTarget& t) { return t.rttReads; };
        if (!waitWithoutEvents([this] {
                return target().get([](ScriptedTarget& t) { return t.pending[0].empty(); });
            })) {
            return false;
        }
        const int start = target().get(reads);
        return waitWithoutEvents([&] { return target().get(reads) >= start + 3; });
    }
    // Waits WITHOUT running the event loop (the owner thread does not drain the session).
    static bool waitWithoutEvents(const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!done()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

private:
    QTemporaryDir m_dir;
    std::shared_ptr<ScriptedTarget> m_target = std::make_shared<ScriptedTarget>();
    std::unique_ptr<SessionRegistry> m_registry;
    std::vector<std::unique_ptr<Recorder>> m_recorders;
    std::atomic<int> m_loads{0};
    std::atomic<std::chrono::milliseconds> m_loadDelay{std::chrono::milliseconds(0)};
    std::atomic<bool> m_loadError{false};
    std::chrono::milliseconds m_attachDeadline{5000};
    std::chrono::milliseconds m_connectRetryDelay{5};
    std::chrono::milliseconds m_loggerWait{50};
    std::chrono::milliseconds m_reconnectInterval{60};
    std::shared_ptr<std::atomic<std::uint64_t>> m_cycles =
        std::make_shared<std::atomic<std::uint64_t>>(0);
};

TEST_F(Session, ConnectsAndDeliversLines) {
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    ASSERT_GE(events->connections.size(), 2U);
    EXPECT_EQ(events->connections.front().state, ConnectionState::Connecting);
    EXPECT_EQ(session->probe().serialNumber, defaultSerial); // set before Connected
    EXPECT_EQ(session->probe().product, "Test Probe");

    target().output(0, "uart:~$ hello\r\n# [00:00:00.010] <wrn> app: legacy log\n");
    target().output(1, "[00:00:01.234,000] <inf> app: Build version: v3.1.0\n");
    ASSERT_TRUE(waitFor([&] { return events->lines.size() >= 3; }));
    EXPECT_EQ(events->texts(Channel::Shell, Direction::Out),
              (std::vector<std::string>{"uart:~$ hello"}));
    const auto logs = events->texts(Channel::Log, Direction::Out);
    ASSERT_EQ(logs.size(), 2U);
    EXPECT_EQ(session->log().lastSeq(), 3U);
    EXPECT_EQ(events->lines.back().seq, 3U);
}

TEST_F(Session, SendLineEchoesOnlyAfterTheWrite) {
    Recorder* events = nullptr;
    target().set([](ScriptedTarget& t) { t.responses["version"] = "Firmware version: v3.1.0\n"; });
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    ASSERT_TRUE(session->sendLine("version"));
    ASSERT_TRUE(waitFor([&] { return events->lines.size() >= 2; }));
    EXPECT_EQ(target().written(), "version\n");
    EXPECT_EQ(events->lines[0].dir, Direction::In);
    EXPECT_EQ(events->lines[0].text, "version");
    EXPECT_EQ(events->lines[1].text, "Firmware version: v3.1.0");
}

TEST_F(Session, LongLinesAreWrittenInDownBufferChunks) {
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    const std::string command(100, 'a');
    ASSERT_TRUE(session->sendLine(command));
    ASSERT_TRUE(waitFor([&] { return !events->lines.empty(); }));
    EXPECT_EQ(target().written(), command + "\n");
}

TEST_F(Session, WriteStallReportsDisconnectAndDataReconnects) {
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    target().set([](ScriptedTarget& t) { t.writeStall = true; });
    ASSERT_TRUE(session->sendLine("version"));
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Disconnected); }));
    ASSERT_TRUE(events->connections.back().error.has_value());
    EXPECT_EQ(events->connections.back().error.value_or(hio::Error{}).code,
              ErrorCode::TargetRttWriteTimeout);
    EXPECT_TRUE(events->texts(Channel::Shell, Direction::In).empty()); // never echoed
    target().output(0, "alive\n");
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Connected); }));
}

TEST_F(Session, ReadFailureReportsDisconnectOnceAndDataClearsIt) {
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    target().set([](ScriptedTarget& t) { t.readFails = true; });
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Disconnected); }));
    EXPECT_EQ(events->connections.back().error.value_or(hio::Error{}).code,
              ErrorCode::TargetRttReadFailed);
    // Many failing reads, still one event (state events only on change).
    ASSERT_TRUE(waitForCount(&ScriptedTarget::rttReads, 20));
    EXPECT_EQ(events->count(ConnectionState::Disconnected), 1);
    target().set([](ScriptedTarget& t) { t.readFails = false; });
    ASSERT_TRUE(waitForCount(&ScriptedTarget::rttReads, 10));
    EXPECT_TRUE(events->reached(ConnectionState::Disconnected)); // empty reads do not clear it
    target().output(0, "back\n");
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Connected); }));
}

TEST_F(Session, PowerLossAfterAHealthyReadingIsDisconnect) {
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    ASSERT_TRUE(waitForCount(&ScriptedTarget::hwStatusCalls, 1)); // a healthy VTref reading
    target().set([](ScriptedTarget& t) { t.vtref = 0; });
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Disconnected); }));
    EXPECT_EQ(events->connections.back().error.value_or(hio::Error{}).code,
              ErrorCode::TargetNoPower);
    EXPECT_EQ(events->connections.back().error.value_or(hio::Error{}).message,
              "The device has no power (0 mV).");
}

TEST_F(Session, ProbeThatAlwaysReportsZeroVoltsNeverTrips) {
    target().set([](ScriptedTarget& t) { t.reportedVtref = 0; }); // cannot measure VTref
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    ASSERT_TRUE(waitForCount(&ScriptedTarget::hwStatusCalls, 3)); // several power checks
    EXPECT_EQ(events->count(ConnectionState::Disconnected), 0);
    EXPECT_TRUE(events->reached(ConnectionState::Connected));
}

// Review R21: a probe that always reports 0 mV and a cable glitch (reads and connects fail). The
// failed connects are target.connect_failed, never target.no_power, the power gate stays off and
// auto-reconnect keeps retrying until the target connects again.
TEST_F(Session, AlwaysZeroVoltProbeWithACableGlitchKeepsRetryingAndRecovers) {
    target().set([](ScriptedTarget& t) { t.reportedVtref = 0; });
    SessionOptions opts = options();
    opts.autoReconnect = true;
    Recorder* events = nullptr;
    auto session = openConnected(opts, events);
    ASSERT_TRUE(session);
    target().set([](ScriptedTarget& t) {
        t.readFails = true;
        t.connectFailures = 1000; // the flat cable is off
    });
    ASSERT_TRUE(waitFor([&] {
        return events->reached(ConnectionState::Disconnected) &&
               events->connections.back().error.value_or(hio::Error{}).code ==
                   ErrorCode::TargetConnectFailed;
    }));
    ASSERT_TRUE(waitForCount(&ScriptedTarget::connectsStarted, 9)); // three more retries
    for (const auto& event : events->connections) {
        EXPECT_NE(event.error.value_or(hio::Error{}).code, ErrorCode::TargetNoPower);
    }
    target().set([](ScriptedTarget& t) {
        t.readFails = false;
        t.connectFailures = 0; // cable back
    });
    EXPECT_TRUE(waitFor([&] { return events->reached(ConnectionState::Connected); }));
}

// Review R21: the first attempt with a probe that reports 0 mV is target.connect_failed and the
// retries go on (no power gate without a healthy reading).
TEST_F(Session, FirstAttemptWithZeroVoltsIsConnectFailedAndKeepsRetrying) {
    target().set([](ScriptedTarget& t) {
        t.reportedVtref = 0;
        t.connectFailures = 6; // the first two attempts (3 connects each) fail
    });
    SessionOptions opts = options();
    opts.autoReconnect = true;
    auto session = open(opts);
    ASSERT_TRUE(session);
    Recorder events(**session);
    EXPECT_TRUE(hio::device::waitForFirstAttempt(**session, std::chrono::milliseconds(5000)));
    EXPECT_TRUE(waitFor([&] { return events.reached(ConnectionState::Connected); }));
    EXPECT_GE(target().get([](ScriptedTarget& t) { return t.connects; }), 7);
    for (const auto& event : events.connections) {
        EXPECT_NE(event.error.value_or(hio::Error{}).code, ErrorCode::TargetNoPower);
    }
}

TEST_F(Session, AutoReconnectOnlyWhilePowered) {
    SessionOptions opts = options();
    opts.autoReconnect = true;
    Recorder* events = nullptr;
    auto session = openConnected(opts, events);
    ASSERT_TRUE(session);
    ASSERT_TRUE(waitForCount(&ScriptedTarget::hwStatusCalls, 1)); // healthy reading
    target().set([](ScriptedTarget& t) { t.vtref = 0; });
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Disconnected); }));
    const int opensWhileUnpowered = target().get([](ScriptedTarget& t) { return t.opens; });
    // Several reconnect intervals (each checks the power) without power: no attempts.
    ASSERT_TRUE(waitForCount(&ScriptedTarget::hwStatusCalls, 4));
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.opens; }), opensWhileUnpowered);
    target().set([](ScriptedTarget& t) { t.vtref = 3300; });
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Connected); }));
    EXPECT_GT(target().get([](ScriptedTarget& t) { return t.opens; }), opensWhileUnpowered);
}

TEST_F(Session, NoAutoReconnectWhenDisabled) {
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    target().set([](ScriptedTarget& t) { t.readFails = true; });
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Disconnected); }));
    const int opens = target().get([](ScriptedTarget& t) { return t.opens; });
    ASSERT_TRUE(waitForCount(&ScriptedTarget::rttReads, 40)); // well over 3 reconnect intervals
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.opens; }), opens);
    // An explicit reconnect is always tried.
    target().set([](ScriptedTarget& t) { t.readFails = false; });
    ASSERT_TRUE(session->reconnect());
    ASSERT_TRUE(waitFor([&] {
        return target().get([](ScriptedTarget& t) { return t.opens; }) > opens &&
               events->reached(ConnectionState::Connected);
    }));
}

TEST_F(Session, SuspendedSessionNeverReattachesAutomatically) {
    SessionOptions opts = options();
    opts.autoReconnect = true;
    Recorder* events = nullptr;
    auto session = openConnected(opts, events);
    ASSERT_TRUE(session);
    ASSERT_TRUE(session->suspend());
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Stopped); }));
    EXPECT_FALSE(events->connections.back().error.has_value());
    EXPECT_FALSE(target().get([](ScriptedTarget& t) { return t.open; })); // probe released
    // Nothing runs while suspended, so only time can show that nothing happens.
    spin(std::chrono::milliseconds(250));
    EXPECT_TRUE(events->reached(ConnectionState::Stopped));
    ASSERT_TRUE(session->resume());
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Connected); }));
}

TEST_F(Session, ResetReattaches) {
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    const int starts = target().get([](ScriptedTarget& t) { return t.rttStarts; });
    ASSERT_TRUE(session->reset("app", hio::device::ResetMode::Go));
    ASSERT_TRUE(waitFor([&] {
        return target().get([](ScriptedTarget& t) { return t.resets; }) == 1 &&
               target().get([](ScriptedTarget& t) { return t.rttStarts; }) > starts;
    }));
    target().output(0, "booted\n");
    ASSERT_TRUE(waitFor([&] { return !events->lines.empty(); }));
    EXPECT_EQ(events->count(ConnectionState::Disconnected), 0); // a plain reset is no disconnect
}

// Review R25: after a halting reset the session does not re-attach automatically (the CPU stays
// halted) until an explicit reconnect; Disconnected without an error, as for a suspend.
TEST_F(Session, HaltResetIsNotReattachedAutomatically) {
    SessionOptions opts = options();
    opts.autoReconnect = true;
    Recorder* events = nullptr;
    auto session = openConnected(opts, events);
    ASSERT_TRUE(session);
    const int starts = target().get([](ScriptedTarget& t) { return t.rttStarts; });
    ASSERT_TRUE(session->reset("app", hio::device::ResetMode::Halt));
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Disconnected); }));
    EXPECT_FALSE(events->connections.back().error.has_value());
    // Review R40: wait for worker cycles instead of a fixed time. Each cycle lasts at least the
    // latency (5 ms) and checks auto-reconnect, so this many cycles span three reconnect
    // intervals (60 ms each), in which an unblocked session would have re-attached.
    const std::uint64_t perInterval =
        static_cast<std::uint64_t>(reconnectInterval().count() / options().latencyMs);
    const std::uint64_t start = cycles();
    ASSERT_TRUE(waitFor([&] { return cycles() >= start + (3 * perInterval); }));
    EXPECT_TRUE(events->reached(ConnectionState::Disconnected));
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.rttStarts; }), starts);
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.connects; }), 1);
    ASSERT_TRUE(session->reconnect()); // explicit (F4)
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Connected); }));
    EXPECT_GT(target().get([](ScriptedTarget& t) { return t.rttStarts; }), starts);
}

// Review R25: a reset without halt ends the halted state (re-attaches at once).
TEST_F(Session, ResetWithoutHaltAfterAHaltResetReattaches) {
    SessionOptions opts = options();
    opts.autoReconnect = true;
    Recorder* events = nullptr;
    auto session = openConnected(opts, events);
    ASSERT_TRUE(session);
    const auto halt = session->reset("app", hio::device::ResetMode::Halt);
    ASSERT_TRUE(halt);
    ASSERT_TRUE(waitFor([&] { return events->result(*halt) != nullptr; })); // one at a time
    EXPECT_TRUE(events->reached(ConnectionState::Disconnected));
    ASSERT_TRUE(session->reset("app", hio::device::ResetMode::Go));
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Connected); }));
}

TEST_F(Session, ResetOnOpen) {
    SessionOptions opts = options();
    opts.resetOnOpen = true;
    Recorder* events = nullptr;
    auto session = openConnected(opts, events);
    ASSERT_TRUE(session);
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.resets; }), 1);
}

TEST_F(Session, SendWhileNotConnectedIsAWarning) {
    useShortAttachDeadline();
    target().set([](ScriptedTarget& t) { t.controlBlockFound = false; });
    SessionOptions opts = options();
    opts.autoReconnect = true;
    auto session = open(opts);
    ASSERT_TRUE(session);
    Recorder events(**session);
    ASSERT_TRUE(waitFor([&] { return events.reached(ConnectionState::Disconnected); }));
    ASSERT_TRUE((*session)->sendLine("version"));
    ASSERT_TRUE(waitFor([&] { return !events.warnings.empty(); }));
    // Review R18: target.not_connected, with the auto-reconnect suggestion.
    EXPECT_EQ(events.warnings.back().code, ErrorCode::TargetNotConnected);
    EXPECT_EQ(events.warnings.back().message, "The device is not connected.");
    EXPECT_EQ(events.warnings.back().suggestion, "Wait for the console to reconnect.");
}

// Review R18: without auto-reconnect the suggestion is F4.
TEST_F(Session, SendAfterSuspendIsNotConnected) {
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    ASSERT_TRUE(session->suspend());
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Stopped); }));
    ASSERT_TRUE(session->sendLine("version"));
    ASSERT_TRUE(waitFor([&] { return !events->warnings.empty(); }));
    EXPECT_EQ(events->warnings.back().code, ErrorCode::TargetNotConnected);
    EXPECT_EQ(events->warnings.back().suggestion, "Reconnect the console (F4).");
    // The probe is released: a reset operation is not possible either (ADR-0062).
    const auto reset = session->reset("app", hio::device::ResetMode::Go);
    ASSERT_TRUE(reset);
    ASSERT_TRUE(waitFor([&] { return events->result(*reset) != nullptr; }));
    ASSERT_FALSE(events->result(*reset)->status);
    EXPECT_EQ(events->result(*reset)->status.error().code, ErrorCode::TargetNotConnected);
}

// Review R35: "Wait for the console to reconnect." only while the session re-attaches by itself
// (auto-reconnect, not halted, suspended or fatally stopped); otherwise F4.
TEST_F(Session, NotConnectedSuggestionWhileHaltedIsReconnect) {
    SessionOptions opts = options();
    opts.autoReconnect = true;
    Recorder* events = nullptr;
    auto session = openConnected(opts, events);
    ASSERT_TRUE(session);
    ASSERT_TRUE(session->reset("app", hio::device::ResetMode::Halt));
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Disconnected); }));
    ASSERT_TRUE(session->sendLine("version"));
    ASSERT_TRUE(waitFor([&] { return !events->warnings.empty(); }));
    EXPECT_EQ(events->warnings.back().code, ErrorCode::TargetNotConnected);
    EXPECT_EQ(events->warnings.back().suggestion, "Reconnect the console (F4).");
}

TEST_F(Session, NotConnectedSuggestionWhileSuspendedIsReconnect) {
    SessionOptions opts = options();
    opts.autoReconnect = true;
    Recorder* events = nullptr;
    auto session = openConnected(opts, events);
    ASSERT_TRUE(session);
    ASSERT_TRUE(session->suspend());
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Stopped); }));
    ASSERT_TRUE(session->sendLine("version"));
    ASSERT_TRUE(waitFor([&] { return !events->warnings.empty(); }));
    EXPECT_EQ(events->warnings.back().code, ErrorCode::TargetNotConnected);
    EXPECT_EQ(events->warnings.back().suggestion, "Reconnect the console (F4).");
}

TEST_F(Session, NotConnectedSuggestionAfterAFatalStopIsReconnect) {
    target().set([](ScriptedTarget& t) { t.probes.clear(); }); // fatal even with auto-reconnect
    SessionOptions opts = options();
    opts.autoReconnect = true;
    auto session = open(opts);
    ASSERT_TRUE(session);
    Recorder events(**session);
    ASSERT_TRUE(waitFor([&] { return events.reached(ConnectionState::Stopped); }));
    ASSERT_TRUE((*session)->sendLine("version"));
    ASSERT_TRUE(waitFor([&] { return !events.warnings.empty(); }));
    EXPECT_EQ(events.warnings.back().code, ErrorCode::TargetNotConnected);
    EXPECT_EQ(events.warnings.back().suggestion, "Reconnect the console (F4).");
}

// Without auto-reconnect, detached after a failed F4 (neither halted, suspended nor fatal).
TEST_F(Session, NotConnectedSuggestionWithoutAutoReconnectIsReconnect) {
    useShortAttachDeadline();
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    target().set([](ScriptedTarget& t) { t.controlBlockFound = false; });
    ASSERT_TRUE(session->reconnect());
    ASSERT_TRUE(waitFor([&] {
        return events->reached(ConnectionState::Disconnected) &&
               events->connections.back().error.value_or(hio::Error{}).code ==
                   ErrorCode::TargetRttNotFound;
    }));
    ASSERT_TRUE(session->sendLine("version"));
    ASSERT_TRUE(waitFor([&] { return !events->warnings.empty(); }));
    EXPECT_EQ(events->warnings.back().code, ErrorCode::TargetNotConnected);
    EXPECT_EQ(events->warnings.back().suggestion, "Reconnect the console (F4).");
}

// ---- fatal and non-fatal open errors -----------------------------------------------------------

TEST_F(Session, NoProbeIsStopped) {
    target().set([](ScriptedTarget& t) { t.probes.clear(); });
    auto session = open(options());
    ASSERT_TRUE(session);
    Recorder events(**session);
    ASSERT_TRUE(waitFor([&] { return events.reached(ConnectionState::Stopped); }));
    EXPECT_EQ(events.connections.back().error.value_or(hio::Error{}).code,
              ErrorCode::ProbeNotFound);
}

TEST_F(Session, LibraryErrorIsStoppedEvenWithAutoReconnect) {
    setLoadError(true);
    SessionOptions opts = options();
    opts.autoReconnect = true;
    auto session = open(opts);
    ASSERT_TRUE(session);
    Recorder events(**session);
    ASSERT_TRUE(waitFor([&] { return events.reached(ConnectionState::Stopped); }));
    EXPECT_EQ(events.connections.back().error.value_or(hio::Error{}).code,
              ErrorCode::DependencyJLinkNotFound);
}

TEST_F(Session, TargetErrorIsStoppedWithoutAutoReconnect) {
    target().set([](ScriptedTarget& t) { t.vtref = 150; }); // unpowered (0 mV: review R21)
    auto session = open(options());
    ASSERT_TRUE(session);
    Recorder events(**session);
    ASSERT_TRUE(waitFor([&] { return events.reached(ConnectionState::Stopped); }));
    EXPECT_EQ(events.connections.back().error.value_or(hio::Error{}).code,
              ErrorCode::TargetNoPower);
}

TEST_F(Session, TargetErrorIsDisconnectedWithAutoReconnectAndRecovers) {
    target().set([](ScriptedTarget& t) { t.vtref = 150; });
    SessionOptions opts = options();
    opts.autoReconnect = true;
    auto session = open(opts);
    ASSERT_TRUE(session);
    Recorder events(**session);
    ASSERT_TRUE(waitFor([&] { return events.reached(ConnectionState::Disconnected); }));
    EXPECT_EQ(events.connections.back().error.value_or(hio::Error{}).code,
              ErrorCode::TargetNoPower);
    target().set([](ScriptedTarget& t) { t.vtref = 3300; });
    ASSERT_TRUE(waitFor([&] { return events.reached(ConnectionState::Connected); }));
}

TEST_F(Session, ConnectRetriesBeforeFailing) {
    target().set([](ScriptedTarget& t) { t.connectFailures = 2; });
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.connects; }), 3);
}

TEST_F(Session, RttNotFoundIsStopped) {
    useShortAttachDeadline();
    target().set([](ScriptedTarget& t) { t.controlBlockFound = false; });
    auto session = open(options());
    ASSERT_TRUE(session);
    Recorder events(**session);
    ASSERT_TRUE(waitFor([&] { return events.reached(ConnectionState::Stopped); }));
    EXPECT_EQ(events.connections.back().error.value_or(hio::Error{}).code,
              ErrorCode::TargetRttNotFound);
}

TEST_F(Session, OutdatedProbeFirmwareIsOneWarning) {
    target().set(
        [](ScriptedTarget& t) { t.libraryFirmware = "J-Link V1 compiled Oct  1 2025 10:00:00"; });
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    ASSERT_TRUE(session->reconnect());
    ASSERT_TRUE(waitFor([&] { return events->count(ConnectionState::Connected) == 2; }));
    int warnings = 0;
    for (const hio::Error& warning : events->warnings) {
        warnings += warning.code == ErrorCode::ProbeFirmwareOutdated ? 1 : 0;
    }
    EXPECT_EQ(warnings, 1);
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.firmware; }),
              "J-Link V1 compiled Oct  1 2024 10:00:00"); // never updated automatically
}

TEST_F(Session, MissingLoggerIsAWarningAndTheConsoleWorks) {
    target().set([](ScriptedTarget& t) { t.up.pop_back(); });
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    ASSERT_EQ(events->warnings.size(), 1U);
    EXPECT_EQ(events->warnings[0].code, ErrorCode::TargetRttBufferNotFound);
}

// ---- open(): validation, registry, locks -------------------------------------------------------

TEST_F(Session, OpenValidatesOptions) {
    SessionOptions opts = options();
    opts.profile = nullptr;
    EXPECT_EQ(open(opts).error().code, ErrorCode::UsageInvalidArgument);
    opts = options();
    opts.targetId = "lte"; // no console
    EXPECT_EQ(open(opts).error().code, ErrorCode::UsageInvalidArgument);
    opts = options();
    opts.targetId = "nope";
    EXPECT_EQ(open(opts).error().code, ErrorCode::UsageInvalidArgument);
    opts = options();
    opts.speedKhz = 1;
    EXPECT_EQ(open(opts).error().message, "The J-Link speed must be between 5 and 50000 kHz.");
    opts = options();
    opts.latencyMs = 0;
    EXPECT_EQ(open(opts).error().code, ErrorCode::UsageInvalidArgument);
    EXPECT_EQ(registry().sessionCount(), 0U);
}

TEST_F(Session, OpenReturnsWithoutTouchingTheLibrary) {
    setLoadDelay(std::chrono::milliseconds(1000));
    // The session creates its own (lazily allocated) log.
    const auto start = std::chrono::steady_clock::now();
    auto session = open(options());
    const auto elapsed = std::chrono::steady_clock::now() - start;
    ASSERT_TRUE(session);
    // Half of the 1000 ms library load it must not wait for (review R46).
    EXPECT_LT(elapsed, std::chrono::milliseconds(500));
    EXPECT_EQ((*session)->connection().state, ConnectionState::Connecting);
}

TEST_F(Session, AtMostEightSessionsAndOnePerProbe) {
    std::vector<std::unique_ptr<DeviceSession>> sessions;
    for (std::uint32_t sn = 1; sn <= SessionRegistry::maxSessions; ++sn) {
        SessionOptions opts = options();
        opts.probeSn = sn;
        auto session = open(opts);
        ASSERT_TRUE(session) << session.error().message;
        sessions.push_back(std::move(*session));
    }
    SessionOptions ninth = options();
    ninth.probeSn = 99;
    EXPECT_EQ(open(ninth).error().code, ErrorCode::InternalSessionLimit);
    sessions.pop_back(); // closes, frees the slot and the lock of SN 8
    SessionOptions again = options();
    again.probeSn = 1;
    const auto duplicate = open(again);
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, ErrorCode::ProbeOpenFailed);
    EXPECT_EQ(duplicate.error().message, "The J-Link 1 is already used by this application.");
    again.probeSn = 8;
    EXPECT_TRUE(open(again));
}

TEST_F(Session, ExplicitSerialTakesTheProbeLock) {
    SessionOptions opts = options();
    opts.probeSn = defaultSerial;
    auto session = open(opts);
    ASSERT_TRUE(session);
    EXPECT_TRUE(hio::device::ProbeLock::owner(defaultSerial, root() / "locks").has_value());
    (*session)->close();
    EXPECT_FALSE(hio::device::ProbeLock::owner(defaultSerial, root() / "locks").has_value());
    EXPECT_EQ(registry().sessionCount(), 0U);
}

TEST_F(Session, FirstProbeSelectionSkipsLockedProbes) {
    target().set([](ScriptedTarget& t) {
        t.probes.push_back(hio::jlink::ProbeInfo{.serialNumber = 222,
                                                 .product = "Test Probe 2",
                                                 .connection = hio::jlink::Connection::Usb,
                                                 .firmware = {}});
    });
    auto held = hio::device::ProbeLock::acquire(defaultSerial, root() / "locks");
    ASSERT_TRUE(held);
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    EXPECT_EQ(session->probe().serialNumber, 222U);
    EXPECT_TRUE(hio::device::ProbeLock::owner(222, root() / "locks").has_value());
    EXPECT_EQ(registry().sessionForProbe(222), session.get());
}

TEST_F(Session, AllProbesLockedIsTheLockError) {
    auto held = hio::device::ProbeLock::acquire(defaultSerial, root() / "locks");
    ASSERT_TRUE(held);
    auto session = open(options());
    ASSERT_TRUE(session);
    Recorder events(**session);
    ASSERT_TRUE(waitFor([&] { return events.reached(ConnectionState::Stopped); }));
    EXPECT_EQ(events.connections.back().error.value_or(hio::Error{}).code,
              ErrorCode::ProbeOpenFailed);
}

// ---- close, queues, threads ------------------------------------------------------------------

TEST_F(Session, CloseDuringEachConnectStepReturnsQuickly) {
    // Closes once the worker is inside the step (`entered`, an observable counter, review R19).
    const auto closeAfter = [&](const std::function<bool()>& entered) {
        auto session = open(options());
        EXPECT_TRUE(session);
        EXPECT_TRUE(waitWithoutEvents(entered));
        const auto start = std::chrono::steady_clock::now();
        (*session)->close();
        return std::chrono::steady_clock::now() - start;
    };
    const auto counter = [this](int ScriptedTarget::* field) {
        const int start = target().get([field](ScriptedTarget& t) { return t.*field; });
        return [this, field, start] {
            return target().get([field](ScriptedTarget& t) { return t.*field; }) > start;
        };
    };
    setLoadDelay(std::chrono::milliseconds(300)); // library load
    const int loads = loadCount();
    EXPECT_LT(closeAfter([&] { return loadCount() > loads; }), std::chrono::milliseconds(1000));
    setLoadDelay(std::chrono::milliseconds(0));
    target().set(
        [](ScriptedTarget& t) { t.openDelay = std::chrono::milliseconds(300); }); // probe open
    EXPECT_LT(closeAfter(counter(&ScriptedTarget::opensStarted)), std::chrono::milliseconds(1000));
    target().set([](ScriptedTarget& t) {
        t.openDelay = std::chrono::milliseconds(0);
        t.connectDelay = std::chrono::milliseconds(300); // target connect
    });
    EXPECT_LT(closeAfter(counter(&ScriptedTarget::connectsStarted)),
              std::chrono::milliseconds(1000));
    target().set([](ScriptedTarget& t) {
        t.connectDelay = std::chrono::milliseconds(0);
        t.controlBlockFound = false; // RTT attach polls until the deadline
    });
    EXPECT_LT(closeAfter(counter(&ScriptedTarget::rttStarts)), std::chrono::milliseconds(1000));
    EXPECT_EQ(registry().sessionCount(), 0U);
}

TEST_F(Session, CloseIsIdempotentAndCommandsFailAfterwards) {
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    session->close();
    session->close();
    const auto status = session->sendLine("version");
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::Cancelled);
    EXPECT_FALSE(target().get([](ScriptedTarget& t) { return t.open; }));
    EXPECT_GE(target().get([](ScriptedTarget& t) { return t.rttStops; }), 1); // rttt: stop RTT
}

TEST_F(Session, FullCommandQueueIsRejected) {
    target().set([](ScriptedTarget& t) {
        t.connectDelay = std::chrono::milliseconds(500);
    }); // worker busy connecting
    auto session = open(options());
    ASSERT_TRUE(session);
    std::optional<hio::Error> rejected;
    for (std::size_t i = 0; i <= DeviceSession::commandQueueCapacity + 1 && !rejected; ++i) {
        const auto status = (*session)->sendLine("x");
        if (!status) {
            rejected = status.error();
        }
    }
    ASSERT_TRUE(rejected.has_value());
    EXPECT_EQ(rejected.value_or(hio::Error{}).code, ErrorCode::InternalQueueFull);
}

TEST_F(Session, EventOverflowIsReportedAsDroppedLines) {
    target().set([](ScriptedTarget& t) { t.up[0].size = 64 * 1024; });
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    std::string burst;
    for (int i = 0; i < 15000; ++i) {
        burst += "x\n";
    }
    target().output(0, burst);
    // The worker reads everything before the owner thread drains the queue (R19: a condition).
    ASSERT_TRUE(waitUntilOutputEmitted());
    ASSERT_TRUE(waitFor([&] { return events->dropped > 0; }))
        << "lines " << events->lines.size() << " dropped " << events->dropped;
    EXPECT_GE(events->dropped, 5000U);
    EXPECT_LE(events->lines.size(), DeviceSession::eventQueueCapacity);
}

TEST_F(Session, ManyCommandsAndClose) {
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(session->sendLine("cmd " + std::to_string(i)));
        if (i % 10 == 0) {
            QCoreApplication::processEvents();
        }
    }
    session->close();
    EXPECT_EQ(registry().sessionCount(), 0U);
}

// ---- waitUntilConnected ----------------------------------------------------------------------

TEST_F(Session, WaitUntilConnectedSuccess) {
    auto session = open(options());
    ASSERT_TRUE(session);
    EXPECT_TRUE(hio::device::waitUntilConnected(**session, std::chrono::milliseconds(5000)));
    EXPECT_EQ((*session)->connection().state, ConnectionState::Connected);
}

TEST_F(Session, WaitUntilConnectedFailure) {
    target().set([](ScriptedTarget& t) { t.probes.clear(); });
    auto session = open(options());
    ASSERT_TRUE(session);
    const auto status = hio::device::waitUntilConnected(**session, std::chrono::milliseconds(5000));
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::ProbeNotFound);
}

TEST_F(Session, WaitUntilConnectedTimeout) {
    setLoadDelay(std::chrono::milliseconds(500));
    auto session = open(options());
    ASSERT_TRUE(session);
    const auto status = hio::device::waitUntilConnected(**session, std::chrono::milliseconds(50));
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::Timeout);
}

TEST_F(Session, WaitUntilConnectedCancelled) {
    setLoadDelay(std::chrono::milliseconds(500));
    auto session = open(options());
    ASSERT_TRUE(session);
    std::stop_source stop;
    stop.request_stop();
    const auto status = hio::device::waitUntilConnected(**session, std::chrono::milliseconds(5000),
                                                        stop.get_token());
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::Cancelled);
}

// ---- console log (ADR-0063) and console file --------------------------------------------------

TEST_F(Session, ProvidedLogReceivesTheLinesOfConsecutiveSessions) {
    auto log = std::make_shared<hio::device::ConsoleLog>(100);
    SessionOptions opts = options();
    opts.consoleLog = log;
    {
        Recorder* events = nullptr;
        auto first = openConnected(opts, events);
        ASSERT_TRUE(first);
        EXPECT_EQ(&first->log(), log.get());
        target().output(0, "first\n");
        ASSERT_TRUE(waitFor([&] { return log->lastSeq() == 1; }));
    }
    log->appendMarker(Channel::Shell, "-- disconnected --");
    {
        Recorder* events = nullptr;
        auto second = openConnected(opts, events);
        ASSERT_TRUE(second);
        target().output(0, "second\n");
        ASSERT_TRUE(waitFor([&] { return log->lastSeq() == 3; }));
    }
    const auto lines = log->linesSince(0);
    ASSERT_EQ(lines.size(), 3U);
    EXPECT_EQ(lines[0].text, "first");
    EXPECT_EQ(lines[1].dir, Direction::Marker);
    EXPECT_EQ(lines[2].text, "second");
}

TEST_F(Session, OwnLogWhenNoneIsProvided) {
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    EXPECT_EQ(session->log().capacityPerChannel(),
              hio::device::ConsoleLog::defaultCapacityPerChannel);
}

TEST_F(Session, ConsoleFileGetsHeaderLinesAndConnectionChanges) {
    const fs::path file = root() / "console.log";
    SessionOptions opts = options();
    opts.consoleFile = file;
    target().set([](ScriptedTarget& t) { t.responses["version"] = "v3.1.0\n"; });
    {
        Recorder* events = nullptr;
        auto session = openConnected(opts, events);
        ASSERT_TRUE(session);
        ASSERT_TRUE(session->sendLine("version"));
        ASSERT_TRUE(waitFor([&] { return events->lines.size() >= 2; }));
    }
    std::ifstream input(file);
    std::stringstream content;
    content << input.rdbuf();
    const std::string text = content.str();
    EXPECT_NE(text.find("Console: CHESTER app, J-Link 801000001"), std::string::npos) << text;
    EXPECT_NE(text.find(" @ connected"), std::string::npos) << text;
    EXPECT_NE(text.find(" < version"), std::string::npos) << text;
    EXPECT_NE(text.find(" > v3.1.0"), std::string::npos) << text;
    EXPECT_NE(text.find(" @ stopped"), std::string::npos) << text;
}

TEST_F(Session, DefaultConsoleFileInTheConsoleDirectory) {
    SessionOptions opts = options();
    opts.consoleFile = std::nullopt;
    Recorder* events = nullptr;
    auto session = openConnected(opts, events);
    ASSERT_TRUE(session);
    bool found = false;
    std::error_code ec;
    for (fs::directory_iterator it(root() / "console", ec), end; !ec && it != end;
         it.increment(ec)) {
        found = found || it->path().filename().string().starts_with("chester-app-");
    }
    EXPECT_TRUE(found);
}

TEST_F(Session, RegistryDestructionClosesSessions) {
    auto session = open(options());
    ASSERT_TRUE(session);
    destroyRegistry();
    EXPECT_FALSE((*session)->sendLine("x"));
}

// ---- review fixes (code review 2026-10-09) and user decisions ---------------------------------

// Review fix 2: a probe that stops answering (USB unplug) no longer blocks auto-reconnect.
TEST_F(Session, AutoReconnectRecoversAfterProbeUnplug) {
    SessionOptions opts = options();
    opts.autoReconnect = true;
    Recorder* events = nullptr;
    auto session = openConnected(opts, events);
    ASSERT_TRUE(session);
    ASSERT_TRUE(waitForCount(&ScriptedTarget::hwStatusCalls, 1)); // a healthy VTref reading
    const auto probes = target().get([](ScriptedTarget& t) { return t.probes; });
    target().set([](ScriptedTarget& t) {
        t.hardwareStatusFails = true; // unplugged
        t.readFails = true;
        t.probes.clear();
    });
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Disconnected); }));
    ASSERT_TRUE(waitForCount(&ScriptedTarget::lists, 2)); // retries while unplugged
    target().set([&probes](ScriptedTarget& t) {
        t.hardwareStatusFails = false; // plugged in again
        t.readFails = false;
        t.probes = probes;
    });
    EXPECT_TRUE(waitFor([&] { return events->reached(ConnectionState::Connected); },
                        std::chrono::milliseconds(5000)));
}

// Review R22: the network-J-Link check (a network scan) runs on the first failed open of the
// probe only, not on every auto-reconnect retry while the USB probe is unplugged.
TEST_F(Session, NetworkProbeCheckRunsOnceDuringRetries) {
    SessionOptions opts = options();
    opts.autoReconnect = true;
    opts.probeSn = defaultSerial;
    Recorder* events = nullptr;
    auto session = openConnected(opts, events);
    ASSERT_TRUE(session);
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.ipLists; }), 0);
    target().set([](ScriptedTarget& t) {
        t.readFails = true;
        t.probes.clear(); // unplugged: every retry fails with probe.not_found
    });
    ASSERT_TRUE(waitFor([&] {
        return events->reached(ConnectionState::Disconnected) &&
               events->connections.back().error.value_or(hio::Error{}).code ==
                   ErrorCode::ProbeNotFound;
    }));
    ASSERT_TRUE(waitForCount(&ScriptedTarget::lists, 5)); // several retries
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.ipLists; }), 1);
}

// Review fix 3: automatic retries report neither Connecting nor an unchanged Disconnected; the
// console file gets the same (de-duplicated) '@' lines. An explicit reconnect reports Connecting.
TEST_F(Session, AutomaticRetriesReportOnlyChanges) {
    const fs::path file = root() / "retries.log";
    SessionOptions opts = options();
    opts.autoReconnect = true;
    opts.consoleFile = file;
    Recorder* events = nullptr;
    auto session = openConnected(opts, events);
    ASSERT_TRUE(session);
    target().set([](ScriptedTarget& t) {
        t.readFails = true;
        t.probes.clear(); // every retry fails with probe.not_found
    });
    ASSERT_TRUE(waitFor([&] {
        return events->reached(ConnectionState::Disconnected) &&
               events->connections.back().error.value_or(hio::Error{}).code ==
                   ErrorCode::ProbeNotFound;
    }));
    ASSERT_TRUE(waitForCount(&ScriptedTarget::lists, 4));       // several automatic retries
    EXPECT_EQ(events->count(ConnectionState::Connecting), 1);   // the initial one only
    EXPECT_EQ(events->count(ConnectionState::Disconnected), 2); // read failed, then not found

    ASSERT_TRUE(session->reconnect()); // explicit: Connecting, then Disconnected again
    ASSERT_TRUE(waitFor([&] {
        return events->count(ConnectionState::Connecting) == 2 &&
               events->count(ConnectionState::Disconnected) == 3;
    }));
    ASSERT_TRUE(waitForCount(&ScriptedTarget::lists, 3)); // further automatic retries stay quiet
    EXPECT_EQ(events->count(ConnectionState::Disconnected), 3);
    session->close();
    std::ifstream input(file);
    int disconnectedLines = 0;
    for (std::string line; std::getline(input, line);) {
        disconnectedLines += line.find(" @ disconnected") != std::string::npos ? 1 : 0;
    }
    EXPECT_EQ(disconnectedLines, events->count(ConnectionState::Disconnected));
}

// Decided 2026-10-09 (rttt): with auto-reconnect a first attempt without power is not fatal; the
// retries wait for power although no healthy reading was ever seen.
// The probe reads a low non-zero VTref (0 mV without a healthy reading is not trusted, R21).
TEST_F(Session, FirstAttemptWithoutPowerWaitsForPowerWithAutoReconnect) {
    target().set([](ScriptedTarget& t) { t.vtref = 150; });
    SessionOptions opts = options();
    opts.autoReconnect = true;
    auto session = open(opts);
    ASSERT_TRUE(session);
    Recorder events(**session);
    EXPECT_TRUE(hio::device::waitForFirstAttempt(**session, std::chrono::milliseconds(5000)));
    EXPECT_EQ((*session)->connection().state, ConnectionState::Disconnected);
    EXPECT_EQ((*session)->connection().error.value_or(hio::Error{}).code, ErrorCode::TargetNoPower);
    const int connects = target().get([](ScriptedTarget& t) { return t.connects; });
    // Retries stay off the probe while unpowered (each interval checks the power).
    ASSERT_TRUE(waitForCount(&ScriptedTarget::hwStatusCalls, 4));
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.connects; }), connects);
    target().set([](ScriptedTarget& t) { t.vtref = 3300; });
    EXPECT_TRUE(waitFor([&] { return events.reached(ConnectionState::Connected); }));
}

TEST_F(Session, WaitForFirstAttemptFailsOnFatalErrors) {
    target().set([](ScriptedTarget& t) { t.probes.clear(); });
    SessionOptions opts = options();
    opts.autoReconnect = true;
    auto session = open(opts);
    ASSERT_TRUE(session);
    const auto status =
        hio::device::waitForFirstAttempt(**session, std::chrono::milliseconds(5000));
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::ProbeNotFound); // probe errors stay fatal
}

// Review fix 9: a session stopped on purpose (no error) ends the wait as cancelled at once.
TEST_F(Session, WaitUntilConnectedOnASuspendedSessionIsCancelled) {
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    ASSERT_TRUE(session->suspend());
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Stopped); }));
    const auto start = std::chrono::steady_clock::now();
    const auto status = hio::device::waitUntilConnected(*session, std::chrono::milliseconds(5000));
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(1000));
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::Cancelled);
}

// ---- independent review 2026-10-10 ------------------------------------------------------------

// Review R10: when the event queue overflows only console lines are dropped; state changes,
// warnings and the probe identity arrive in order, and the drop count is exact.
TEST_F(Session, EventOverflowDropsOnlyConsoleLines) {
    target().set([](ScriptedTarget& t) {
        t.up[0].size = 64 * 1024;
        t.downBuffers[0].size = 0; // sendLine() gives a warning (unsized buffer)
    });
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    const std::size_t linesBefore = events->lines.size();
    const auto connectedBefore = events->count(ConnectionState::Connected);
    std::string burst;
    for (int i = 0; i < 15000; ++i) {
        burst += "x\n";
    }
    // From here on the owner thread does not drain: the queue (10 000) overflows.
    target().output(0, burst);
    ASSERT_TRUE(waitUntilOutputEmitted());
    ASSERT_TRUE(session->sendLine("version")); // warning
    target().set([](ScriptedTarget& t) { t.probes[0].product = "Renamed Probe"; });
    ASSERT_TRUE(session->reconnect()); // ProbeIdentified, Connecting, Connected
    ASSERT_TRUE(waitWithoutEvents(
        [this] { return target().get([](ScriptedTarget& t) { return t.opens; }) >= 2; }));
    target().output(0, burst);
    ASSERT_TRUE(waitUntilOutputEmitted());
    ASSERT_TRUE(waitFor([&] {
        return events->count(ConnectionState::Connected) == connectedBefore + 1 &&
               events->lines.size() - linesBefore + events->dropped == 30000U;
    })) << "lines "
        << events->lines.size() - linesBefore << " dropped " << events->dropped;
    EXPECT_GT(events->dropped, 0U);
    EXPECT_EQ(events->connections.back().state, ConnectionState::Connected);
    EXPECT_EQ(session->probe().product, "Renamed Probe"); // ProbeIdentified arrived
    ASSERT_FALSE(events->warnings.empty());
    EXPECT_EQ(events->warnings.back().code, ErrorCode::TargetRttBufferNotFound);
}

// Review R13: a power loss is detected while the target keeps printing.
TEST_F(Session, PowerLossIsDetectedDuringContinuousOutput) {
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    target().set([](ScriptedTarget& t) { t.vtref = 0; });
    EXPECT_TRUE(waitFor([&] {
        target().output(0, "still printing\n"); // data every event-loop round
        return events->reached(ConnectionState::Disconnected);
    }));
    EXPECT_EQ(events->connections.back().error.value_or(hio::Error{}).code,
              ErrorCode::TargetNoPower);
}

// Review R31: retries that fail the same way (firmware without RTT: every attach ends with
// target.rtt_not_found after its deadline, which is as long as the reconnect interval here) are
// paced from the END of each attempt, not run back to back; a command sent meanwhile is handled
// within one attempt plus one cycle.
TEST_F(Session, FailedRetriesArePacedFromTheEndOfEachAttempt) {
    const auto interval = std::chrono::milliseconds(300);
    const auto deadline = std::chrono::milliseconds(300);
    setReconnectInterval(interval);
    setAttachDeadline(deadline);
    target().set([](ScriptedTarget& t) { t.controlBlockFound = false; });
    SessionOptions opts = options();
    opts.autoReconnect = true;
    auto session = open(opts);
    ASSERT_TRUE(session);
    Recorder events(**session);
    ASSERT_TRUE(waitFor([&] {
        return events.reached(ConnectionState::Disconnected) &&
               events.connections.back().error.value_or(hio::Error{}).code ==
                   ErrorCode::TargetRttNotFound;
    }));
    ASSERT_TRUE(
        waitFor([&] { return target().get([](ScriptedTarget& t) { return t.rttStarts; }) >= 2; }));
    const auto sent = std::chrono::steady_clock::now();
    ASSERT_TRUE((*session)->sendLine("version"));
    ASSERT_TRUE(waitFor([&] { return !events.warnings.empty(); }));
    const auto handled = std::chrono::steady_clock::now() - sent;
    EXPECT_EQ(events.warnings.back().code, ErrorCode::TargetNotConnected);
    // One attempt (the attach deadline) plus one cycle, with slack for the sanitizer builds.
    EXPECT_LT(handled, deadline + std::chrono::milliseconds(1000));
    ASSERT_TRUE(
        waitFor([&] { return target().get([](ScriptedTarget& t) { return t.rttStarts; }) >= 5; }));
    const auto [starts, stops] =
        target().get([](ScriptedTarget& t) { return std::pair(t.rttStartTimes, t.rttStopTimes); });
    // Each attempt ends with rttStop() at its deadline; the next rttStart() is at least one
    // reconnect interval later.
    for (std::size_t k = 1; k < starts.size(); ++k) {
        std::optional<std::chrono::steady_clock::time_point> previousEnd;
        for (const auto stop : stops) {
            if (stop <= starts[k]) {
                previousEnd = stop;
            }
        }
        ASSERT_TRUE(previousEnd.has_value()) << "attempt " << k;
        EXPECT_GE(starts[k] - previousEnd.value_or(starts[k]), interval)
            << "attempt " << k << " started "
            << std::chrono::duration_cast<std::chrono::milliseconds>(
                   starts[k] - previousEnd.value_or(starts[k]))
                   .count()
            << " ms after the previous one ended";
    }
    for (const auto& event : events.connections) {
        EXPECT_NE(event.state, ConnectionState::Connected);
    }
}

// Review R33: a probe that always reads 0 mV and one failed status read: the next successful
// read counts as powered, so data reports Connected again.
TEST_F(Session, AlwaysZeroVoltProbeRecoversAfterAFailedStatusRead) {
    target().set([](ScriptedTarget& t) { t.reportedVtref = 0; });
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    target().set([](ScriptedTarget& t) { t.hardwareStatusFails = true; });
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Disconnected); }));
    target().set([](ScriptedTarget& t) { t.hardwareStatusFails = false; });
    ASSERT_TRUE(waitForCount(&ScriptedTarget::hwStatusCalls, 2)); // successful reads again
    EXPECT_TRUE(waitFor([&] {
        target().output(0, "data\n");
        return events->reached(ConnectionState::Connected);
    }));
}

// Review R43: each term of connectTimeoutFor() dominates one first connect below (the other terms
// are small). The attempt ends with its own result (target.rtt_not_found, or Connected), not with
// `timeout`, and it takes longer than the timeout would be without that term: dropping the term
// from the formula makes the test fail.
class ConnectTimeout : public Session {
protected:
    struct Attempt {
        hio::Status status;
        std::chrono::milliseconds elapsed{0};
    };

    // The timeout of the test timings with `budget`; checked against the hand-computed value.
    std::chrono::milliseconds timeoutFor(const hio::device::ConnectTimeoutBudget& budget,
                                         std::chrono::milliseconds handComputed) {
        const hio::device::detail::SessionTiming timing = hooks().timing;
        const auto timeout = hio::device::connectTimeoutFor(timing.probe, timing.rtt, budget);
        EXPECT_EQ(timeout, handComputed);
        return timeout;
    }

    Attempt connectWithin(std::chrono::milliseconds timeout) {
        auto session = open(options());
        EXPECT_TRUE(session);
        if (!session) {
            return {.status = hio::fail(ErrorCode::InternalUnexpected)};
        }
        const auto begin = std::chrono::steady_clock::now();
        Attempt attempt{.status = hio::device::waitUntilConnected(**session, timeout)};
        attempt.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - begin);
        return attempt;
    }

    static void expectNoRtt(const Attempt& attempt) {
        ASSERT_FALSE(attempt.status);
        EXPECT_EQ(attempt.status.error().code, ErrorCode::TargetRttNotFound)
            << attempt.status.error().detail;
    }

    // Small terms: 5 ms retry delay, 50 ms logger wait (the fixture's), 100 ms attach deadline.
    void SetUp() override { setAttachDeadline(std::chrono::milliseconds(100)); }
};

// Review R38 / R43: connectTimeoutFor() against hand-computed values. Production timings:
// 5 s library load + 3 x (3 s connect + 0.5 s retry delay) + 15 s attach deadline + 3 s logger
// wait + 2 s margin = 35.5 s. With distinct powers of ten per term, a term that is dropped,
// counted twice or not multiplied by the connect attempts changes the digits.
TEST_F(ConnectTimeout, HandComputedValues) {
    using std::chrono::milliseconds;
    EXPECT_EQ(hio::device::connectTimeoutFor(hio::jlink::ProbeTiming{}, hio::jlink::RttTiming{}),
              milliseconds(35500));
    EXPECT_EQ(hio::device::defaultConnectTimeout, milliseconds(35500));
    hio::jlink::ProbeTiming probe;
    probe.connectAttempts = 3;
    probe.connectRetryDelay = milliseconds(100);
    hio::jlink::RttTiming rtt;
    rtt.attachDeadline = milliseconds(1000);
    rtt.loggerWait = milliseconds(10000);
    const hio::device::ConnectTimeoutBudget budget{.libraryLoad = milliseconds(1),
                                                   .connectAttempt = milliseconds(10),
                                                   .margin = milliseconds(100000)};
    // 1 + 3 x (10 + 100) + 1000 + 10000 + 100000
    EXPECT_EQ(hio::device::connectTimeoutFor(probe, rtt, budget), milliseconds(111331));
}

TEST_F(ConnectTimeout, LibraryLoadTermCoversASlowLoad) {
    using std::chrono::milliseconds;
    setLoadDelay(milliseconds(800));
    target().set([](ScriptedTarget& t) { t.controlBlockFound = false; });
    const hio::device::ConnectTimeoutBudget budget{.libraryLoad = milliseconds(1000),
                                                   .connectAttempt = milliseconds(50),
                                                   .margin = milliseconds(300)};
    // 1000 + 3 x (50 + 5) + 100 + 50 + 300
    const auto timeout = timeoutFor(budget, milliseconds(1615));
    const Attempt attempt = connectWithin(timeout);
    expectNoRtt(attempt);
    EXPECT_GT(attempt.elapsed, timeout - budget.libraryLoad);
}

TEST_F(ConnectTimeout, ConnectAttemptTermCoversThreeSlowConnects) {
    using std::chrono::milliseconds;
    target().set([](ScriptedTarget& t) {
        t.connectFailures = 2;
        t.connectDelay = milliseconds(450);
        t.controlBlockFound = false;
    });
    const hio::device::ConnectTimeoutBudget budget{.libraryLoad = milliseconds(100),
                                                   .connectAttempt = milliseconds(500),
                                                   .margin = milliseconds(300)};
    // 100 + 3 x (500 + 5) + 100 + 50 + 300
    const auto timeout = timeoutFor(budget, milliseconds(2065));
    const Attempt attempt = connectWithin(timeout);
    expectNoRtt(attempt);
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.connects; }), 3);
    // Not covered by one connect attempt alone (the term must be multiplied by the attempts).
    EXPECT_GT(attempt.elapsed, timeout - (2 * budget.connectAttempt));
}

TEST_F(ConnectTimeout, RetryDelayTermCoversTheDelaysBetweenConnects) {
    using std::chrono::milliseconds;
    setConnectRetryDelay(milliseconds(500));
    target().set([](ScriptedTarget& t) {
        t.connectFailures = 2;
        t.controlBlockFound = false;
    });
    const hio::device::ConnectTimeoutBudget budget{.libraryLoad = milliseconds(100),
                                                   .connectAttempt = milliseconds(50),
                                                   .margin = milliseconds(300)};
    // 100 + 3 x (50 + 500) + 100 + 50 + 300
    const auto timeout = timeoutFor(budget, milliseconds(2200));
    const Attempt attempt = connectWithin(timeout);
    expectNoRtt(attempt);
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.connects; }), 3);
    EXPECT_GT(attempt.elapsed, timeout - (3 * milliseconds(500)));
}

TEST_F(ConnectTimeout, AttachDeadlineTermCoversFirmwareWithoutRtt) {
    using std::chrono::milliseconds;
    setAttachDeadline(milliseconds(1000));
    target().set([](ScriptedTarget& t) { t.controlBlockFound = false; });
    const hio::device::ConnectTimeoutBudget budget{.libraryLoad = milliseconds(100),
                                                   .connectAttempt = milliseconds(50),
                                                   .margin = milliseconds(300)};
    // 100 + 3 x (50 + 5) + 1000 + 50 + 300
    const auto timeout = timeoutFor(budget, milliseconds(1615));
    const Attempt attempt = connectWithin(timeout);
    expectNoRtt(attempt);
    EXPECT_GT(attempt.elapsed, timeout - milliseconds(1000));
}

// The terminal is found at once, the logger buffer never: the session connects without logs after
// the logger wait (which runs after the attach deadline's search, not inside it).
TEST_F(ConnectTimeout, LoggerWaitTermCoversALateLogger) {
    using std::chrono::milliseconds;
    setLoggerWait(milliseconds(1000));
    target().set(
        [](ScriptedTarget& t) { t.up = {{.name = "Terminal", .size = 1024, .flags = 0}}; });
    const hio::device::ConnectTimeoutBudget budget{.libraryLoad = milliseconds(100),
                                                   .connectAttempt = milliseconds(50),
                                                   .margin = milliseconds(300)};
    // 100 + 3 x (50 + 5) + 100 + 1000 + 300
    const auto timeout = timeoutFor(budget, milliseconds(1665));
    const Attempt attempt = connectWithin(timeout);
    ASSERT_TRUE(attempt.status) << attempt.status.error().detail;
    EXPECT_GT(attempt.elapsed, timeout - milliseconds(1000));
}

// The margin absorbs delays outside the modelled terms; here a probe open that takes longer than
// the library-load budget.
TEST_F(ConnectTimeout, MarginCoversOtherDelays) {
    using std::chrono::milliseconds;
    target().set([](ScriptedTarget& t) {
        t.openDelay = milliseconds(700);
        t.controlBlockFound = false;
    });
    const hio::device::ConnectTimeoutBudget budget{.libraryLoad = milliseconds(100),
                                                   .connectAttempt = milliseconds(50),
                                                   .margin = milliseconds(1000)};
    // 100 + 3 x (50 + 5) + 100 + 50 + 1000
    const auto timeout = timeoutFor(budget, milliseconds(1415));
    const Attempt attempt = connectWithin(timeout);
    expectNoRtt(attempt);
    EXPECT_GT(attempt.elapsed, timeout - budget.margin);
}

// ---- ADR-0091: the target voltage in connection events ---------------------------------------

TEST_F(Session, ConnectedEventCarriesTheTargetVoltage) {
    target().set([](ScriptedTarget& t) { t.vtref = 3280; });
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    EXPECT_EQ(events->connections.back().targetMillivolts, 3280);
    EXPECT_FALSE(events->connections.front().targetMillivolts.has_value()); // Connecting
    // A power loss is reported with the voltage that was measured.
    target().set([](ScriptedTarget& t) { t.reportedVtref = 400; });
    ASSERT_TRUE(waitFor([&] { return events->reached(ConnectionState::Disconnected); }));
    EXPECT_EQ(events->connections.back().targetMillivolts, 400);
}

TEST_F(Session, ProbeThatCannotMeasureReportsNoVoltage) {
    target().set([](ScriptedTarget& t) { t.reportedVtref = 0; }); // always 0 mV
    Recorder* events = nullptr;
    auto session = openConnected(options(), events);
    ASSERT_TRUE(session);
    EXPECT_FALSE(events->connections.back().targetMillivolts.has_value());
}

} // namespace
