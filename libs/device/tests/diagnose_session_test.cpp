// Probe-only sessions, startConnection and the Diagnose operation (plan/features/0007, B1 + B2;
// ADR-0069) with the in-process scripted J-Link. Run under ASan/UBSan and TSan by the presets.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/device_session.h"
#include "hio/device/operation.h"
#include "hio/device/probe_lock.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/device/session_registry.h"
#include "hio/diag/check.h"
#include "hio/firmware/program_progress.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/library_locator.h"
#include "scripted_api.h"
#include "session_internal.h"
#include "session_test_support.h"

#include <QObject>
#include <QTemporaryDir>
#include <QtSystemDetection>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;
using hio::ErrorCode;
using hio::device::ConnectionState;
using hio::device::DeviceSession;
using hio::device::DiagnoseStep;
using hio::device::OperationId;
using hio::device::OperationKind;
using hio::device::OperationResult;
using hio::device::SessionOptions;
using hio::device::SessionRegistry;
using hio::device::detail::OpenHooks;
using hio::device::test::Recorder;
using hio::device::test::ScriptedApi;
using hio::device::test::ScriptedTarget;
using hio::device::test::spin;
using hio::device::test::waitFor;
using hio::diag::CheckStatus;
using hio::firmware::ProgramPhase;

constexpr std::uint32_t probeSerial = 801000001;

class Diagnose : public ::testing::Test {
protected:
    Diagnose()
        : m_registry(std::make_unique<SessionRegistry>(
              hio::device::RegistryOptions{.instanceRoot = root() / "instances",
                                           .lockDirectory = root() / "locks",
                                           .consoleDirectory = root() / "console"})) {}

    [[nodiscard]] fs::path root() const { return {m_dir.path().toStdString()}; }

    [[nodiscard]] static SessionOptions options(bool connectTarget,
                                                std::string_view target = "app") {
        SessionOptions options;
        options.profile = hio::device::ProfileRegistry::find("chester");
        options.targetId = target;
        options.probeSn = probeSerial;
        options.latencyMs = 5;
        options.attachRtt = target == "app";
        options.connectTarget = connectTarget;
        options.consoleFile = fs::path();
        return options;
    }

    [[nodiscard]] OpenHooks hooks() const {
        OpenHooks hooks;
        hooks.apiFactory = [target =
                                m_target]() -> hio::Result<std::unique_ptr<hio::jlink::IJLinkApi>> {
            return std::make_unique<ScriptedApi>(target);
        };
        hooks.timing.powerCheckInterval = std::chrono::milliseconds(20);
        hooks.timing.reconnectInterval = std::chrono::milliseconds(60);
        hooks.timing.probe.connectRetryDelay = std::chrono::milliseconds(5);
        hooks.timing.rtt.attachDeadline = std::chrono::milliseconds(5000);
        hooks.timing.rtt.pollInterval = std::chrono::milliseconds(5);
        hooks.timing.rtt.loggerWait = std::chrono::milliseconds(50);
        hooks.timing.rtt.staleRetryDelay = std::chrono::milliseconds(5);
        hooks.timing.diagnoseRttBudget = std::chrono::milliseconds(300);
        if (m_diagnoseBudget) {
            hooks.timing.diagnoseBudget = *m_diagnoseBudget;
        }
        hooks.usbSysfsRoot = root() / "sys"; // no SEGGER device unless a test adds one
        hooks.usbDevRoot = root() / "dev";
        return hooks;
    }

    std::unique_ptr<DeviceSession> open(const SessionOptions& opts, ConnectionState until) {
        auto session = hio::device::detail::openSession(*m_registry, hio::jlink::LibraryLocation{},
                                                        opts, hooks());
        EXPECT_TRUE(session) << session.error().message;
        if (!session) {
            return nullptr;
        }
        m_events = std::make_unique<Recorder>(**session);
        QObject::connect(
            session->get(), &DeviceSession::diagnoseStep, &m_events->context,
            [this](OperationId id, const DiagnoseStep& step) { m_steps.emplace_back(id, step); });
        EXPECT_TRUE(waitFor([&] { return m_events->reached(until); }));
        return std::move(*session);
    }

    const OperationResult* finish(const hio::Result<OperationId>& id) {
        EXPECT_TRUE(id) << (id ? std::string() : id.error().message);
        if (!id) {
            return nullptr;
        }
        EXPECT_TRUE(waitFor([&] { return events().result(*id) != nullptr; }));
        return events().result(*id);
    }

    [[nodiscard]] static std::vector<std::string> statuses(const OperationResult& result) {
        std::vector<std::string> list;
        list.reserve(result.checks.size());
        for (const DiagnoseStep& step : result.checks) {
            list.push_back(step.id + ":" + std::string(hio::diag::checkStatusId(step.status)));
        }
        return list;
    }

    [[nodiscard]] static const DiagnoseStep& step(const OperationResult& result,
                                                  std::string_view id) {
        for (const DiagnoseStep& s : result.checks) {
            if (s.id == id) {
                return s;
            }
        }
        static const DiagnoseStep none;
        ADD_FAILURE() << "no step " << id;
        return none;
    }

    [[nodiscard]] static ErrorCode codeOf(const DiagnoseStep& step) {
        return step.error ? step.error->code : ErrorCode::InternalUnexpected;
    }

    // No reset, halt, run or write reached the target (ADR-0069).
    void expectNoWrites() {
        const auto ops = target().get([](ScriptedTarget& t) { return t.ops; });
        for (const std::string& op : ops) {
            EXPECT_TRUE(op.starts_with("connect:")) << op;
        }
        EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.resets; }), 0);
    }

    // The overall Diagnose budget of the sessions opened next (default: the production value).
    void setDiagnoseBudget(std::chrono::milliseconds budget) { m_diagnoseBudget = budget; }

    [[nodiscard]] Recorder& events() { return *m_events; }
    [[nodiscard]] ScriptedTarget& target() { return *m_target; }
    [[nodiscard]] const std::vector<std::pair<OperationId, DiagnoseStep>>& steps() const {
        return m_steps;
    }

private:
    QTemporaryDir m_dir;
    std::shared_ptr<ScriptedTarget> m_target = std::make_shared<ScriptedTarget>();
    std::unique_ptr<SessionRegistry> m_registry;
    std::unique_ptr<Recorder> m_events;
    std::vector<std::pair<OperationId, DiagnoseStep>> m_steps;
    std::optional<std::chrono::milliseconds> m_diagnoseBudget;
};

TEST_F(Diagnose, ProbeOnlySessionNeverConnectsTheTarget) {
    auto session = open(options(false), ConnectionState::ProbeOpen);
    ASSERT_TRUE(session);
    spin(std::chrono::milliseconds(150));
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.connects; }), 0);
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.rttStarts; }), 0);
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.hwStatusCalls; }), 0); // no polling
    EXPECT_TRUE(target().get([](ScriptedTarget& t) { return t.open; }));
    EXPECT_EQ(session->probe().serialNumber, probeSerial);
    EXPECT_TRUE(events().reached(ConnectionState::ProbeOpen));
    // The probe lock is held (ADR-0058).
    const auto lock = hio::device::ProbeLock::acquire(probeSerial, root() / "locks");
    EXPECT_FALSE(lock.has_value());
}

TEST_F(Diagnose, StartConnectionPromotesAndKeepsTheProbe) {
    auto session = open(options(false), ConnectionState::ProbeOpen);
    ASSERT_TRUE(session);
    ASSERT_TRUE(session->startConnection(true));
    ASSERT_TRUE(waitFor([&] { return events().reached(ConnectionState::Connected); }));
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.opens; }), 1); // not reopened
    EXPECT_GT(target().get([](ScriptedTarget& t) { return t.rttStarts; }), 0);
    EXPECT_FALSE(hio::device::ProbeLock::acquire(probeSerial, root() / "locks").has_value());
    // A normal session cannot be promoted again.
    const auto again = session->startConnection(true);
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error().code, ErrorCode::UsageInvalidArgument);
}

// Review V5: a second startConnection() before the session left ProbeOpen is a no-op, so the
// target is connected and RTT attached once.
TEST_F(Diagnose, StartConnectionTwiceConnectsOnce) {
    auto session = open(options(false), ConnectionState::ProbeOpen);
    ASSERT_TRUE(session);
    const int connecting = events().count(ConnectionState::Connecting); // the probe open
    ASSERT_TRUE(session->startConnection(true));
    ASSERT_TRUE(session->startConnection(true)); // still ProbeOpen here: the facade knows
    ASSERT_TRUE(waitFor([&] { return events().reached(ConnectionState::Connected); }));
    spin(std::chrono::milliseconds(100));
    EXPECT_EQ(events().count(ConnectionState::Connecting), connecting + 1);
    EXPECT_EQ(events().count(ConnectionState::Connected), 1);
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.connects; }), 1);
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.rttStarts; }), 1);
    // Promoted: refused as before (not ProbeOpen).
    const auto again = session->startConnection(true);
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error().code, ErrorCode::UsageInvalidArgument);
}

// Review V12: a probe-only session writes no console file (no header-only file, no rotation of
// the probe's file) until it is promoted.
TEST_F(Diagnose, ProbeOnlySessionOpensTheConsoleFileOnlyWhenPromoted) {
    SessionOptions opts = options(false);
    opts.consoleFile = std::nullopt; // the default file in the registry's console directory
    const auto files = [this] {
        std::size_t count = 0;
        std::error_code error;
        if (fs::exists(root() / "console", error)) {
            for (const auto& entry : fs::recursive_directory_iterator(root() / "console", error)) {
                count += entry.is_regular_file() ? 1U : 0U;
            }
        }
        return count;
    };
    auto session = open(opts, ConnectionState::ProbeOpen);
    ASSERT_TRUE(session);
    EXPECT_NE(finish(session->diagnose("app")), nullptr);
    spin(std::chrono::milliseconds(50));
    EXPECT_EQ(files(), 0U);
    ASSERT_TRUE(session->startConnection(true));
    ASSERT_TRUE(waitFor([&] { return events().reached(ConnectionState::Connected); }));
    EXPECT_TRUE(waitFor([&] { return files() == 1U; }));
}

TEST_F(Diagnose, StartConnectionWithRttOnTheLteTargetIsRefused) {
    auto session = open(options(false, "lte"), ConnectionState::ProbeOpen);
    ASSERT_TRUE(session);
    const auto refused = session->startConnection(true);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::UsageInvalidArgument);
    ASSERT_TRUE(session->startConnection(false));
    EXPECT_TRUE(waitFor([&] { return events().reached(ConnectionState::Connected); }));
}

TEST_F(Diagnose, AllGoodOnAProbeOnlySession) {
    auto session = open(options(false), ConnectionState::ProbeOpen);
    ASSERT_TRUE(session);
    const auto id = session->diagnose("app");
    const OperationResult* result = finish(id);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->status);
    EXPECT_EQ(result->kind, OperationKind::Diagnose);
    EXPECT_EQ(statuses(*result),
              (std::vector<std::string>{"probe:ok", "power:ok", "connect:ok", "identify:ok",
                                        "rtt:ok", "rtt-buffers:ok"}));
    EXPECT_EQ(step(*result, "probe").result, "Test Probe · 801000001");
    EXPECT_EQ(step(*result, "power").result, "3.30 V");
    EXPECT_EQ(step(*result, "connect").result, "Connected (SWD, automatic speed)");
    EXPECT_EQ(step(*result, "identify").result, "nRF52840, not protected");
    EXPECT_EQ(step(*result, "rtt-buffers").result, "Terminal, Logger");
    // One diagnoseStep signal per step, in order, before the result.
    ASSERT_EQ(steps().size(), 6U);
    for (std::size_t i = 0; i < steps().size(); ++i) {
        EXPECT_EQ(steps()[i].first, *id);
        EXPECT_EQ(steps()[i].second.id, hio::device::diagnoseStepIds.at(i));
    }
    EXPECT_EQ(events().phases(*id), std::vector<ProgramPhase>{ProgramPhase::Connecting});
    expectNoWrites();
    // RTT was stopped again; the session stays probe-only.
    EXPECT_FALSE(target().get([](ScriptedTarget& t) { return t.rttStarted; }));
    EXPECT_TRUE(events().reached(ConnectionState::ProbeOpen));
    EXPECT_EQ(events().count(ConnectionState::Connected), 0);
}

TEST_F(Diagnose, NoPowerSkipsTheRest) {
    target().set([](ScriptedTarget& t) { t.vtref = 500; });
    auto session = open(options(false), ConnectionState::ProbeOpen);
    ASSERT_TRUE(session);
    const OperationResult* result = finish(session->diagnose("app"));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(statuses(*result),
              (std::vector<std::string>{"probe:ok", "power:failed", "connect:skipped",
                                        "identify:skipped", "rtt:skipped", "rtt-buffers:skipped"}));
    ASSERT_TRUE(step(*result, "power").error);
    EXPECT_EQ(codeOf(step(*result, "power")), ErrorCode::TargetNoPower);
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.connects; }), 0);
}

TEST_F(Diagnose, ZeroVoltsFromAProbeThatCannotMeasureIsNotNoPower) {
    target().set([](ScriptedTarget& t) { t.reportedVtref = 0; });
    auto session = open(options(false), ConnectionState::ProbeOpen);
    ASSERT_TRUE(session);
    const OperationResult* result = finish(session->diagnose("app"));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(step(*result, "power").status, CheckStatus::Ok);
    EXPECT_EQ(step(*result, "power").result, "Not measured by this J-Link");
    EXPECT_EQ(step(*result, "connect").status, CheckStatus::Ok);
}

TEST_F(Diagnose, ConnectFailureIsOneAttempt) {
    target().set([](ScriptedTarget& t) { t.connectFailures = 1; });
    auto session = open(options(false), ConnectionState::ProbeOpen);
    ASSERT_TRUE(session);
    const OperationResult* result = finish(session->diagnose("app"));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(step(*result, "connect").status, CheckStatus::Failed);
    ASSERT_TRUE(step(*result, "connect").error);
    EXPECT_EQ(codeOf(step(*result, "connect")), ErrorCode::TargetConnectFailed);
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.connects; }), 1);
    EXPECT_EQ(step(*result, "identify").status, CheckStatus::Skipped);
}

TEST_F(Diagnose, WrongFamilyFailsIdentify) {
    // The flat cable is on the LTE connector: the "app" MCU answers as an nRF91.
    target().set([](ScriptedTarget& t) { t.app = hio::device::test::nrf91Chip(); });
    auto session = open(options(false), ConnectionState::ProbeOpen);
    ASSERT_TRUE(session);
    const OperationResult* result = finish(session->diagnose("app"));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(step(*result, "identify").status, CheckStatus::Failed);
    ASSERT_TRUE(step(*result, "identify").error);
    EXPECT_EQ(codeOf(step(*result, "identify")), ErrorCode::TargetWrongFamily);
    EXPECT_EQ(step(*result, "rtt").status, CheckStatus::Skipped);
    expectNoWrites();
}

TEST_F(Diagnose, ProtectedIsAWarningAndSkipsRtt) {
    target().set([](ScriptedTarget& t) { t.app.protectedChip = true; });
    auto session = open(options(false), ConnectionState::ProbeOpen);
    ASSERT_TRUE(session);
    const OperationResult* result = finish(session->diagnose("app"));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(step(*result, "identify").status, CheckStatus::Warning);
    ASSERT_TRUE(step(*result, "identify").error);
    EXPECT_EQ(codeOf(step(*result, "identify")), ErrorCode::TargetProtected);
    EXPECT_EQ(step(*result, "rtt").status, CheckStatus::Skipped);
    EXPECT_EQ(step(*result, "rtt-buffers").status, CheckStatus::Skipped);
    for (const DiagnoseStep& s : result->checks) {
        EXPECT_NE(s.status, CheckStatus::Failed) << s.id;
    }
    expectNoWrites();
    EXPECT_TRUE(target().get([](ScriptedTarget& t) { return t.app.protectedChip; }));
}

TEST_F(Diagnose, NoRttControlBlock) {
    target().set([](ScriptedTarget& t) { t.controlBlockFound = false; });
    auto session = open(options(false), ConnectionState::ProbeOpen);
    ASSERT_TRUE(session);
    const OperationResult* result = finish(session->diagnose("app"));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(step(*result, "rtt").status, CheckStatus::Failed);
    ASSERT_TRUE(step(*result, "rtt").error);
    EXPECT_EQ(codeOf(step(*result, "rtt")), ErrorCode::TargetRttNotFound);
    EXPECT_EQ(step(*result, "rtt-buffers").status, CheckStatus::Skipped);
}

TEST_F(Diagnose, MissingLoggerBufferIsAWarning) {
    target().set(
        [](ScriptedTarget& t) { t.up = {{.name = "Terminal", .size = 1024, .flags = 0}}; });
    auto session = open(options(false), ConnectionState::ProbeOpen);
    ASSERT_TRUE(session);
    const OperationResult* result = finish(session->diagnose("app"));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(step(*result, "rtt").status, CheckStatus::Ok);
    EXPECT_EQ(step(*result, "rtt-buffers").status, CheckStatus::Warning);
    ASSERT_TRUE(step(*result, "rtt-buffers").error);
    EXPECT_EQ(codeOf(step(*result, "rtt-buffers")), ErrorCode::TargetRttBufferNotFound);
}

TEST_F(Diagnose, MissingTerminalBufferFails) {
    target().set([](ScriptedTarget& t) { t.up = {{.name = "Logger", .size = 1024, .flags = 0}}; });
    auto session = open(options(false), ConnectionState::ProbeOpen);
    ASSERT_TRUE(session);
    const OperationResult* result = finish(session->diagnose("app"));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(step(*result, "rtt").status, CheckStatus::Ok);
    EXPECT_EQ(step(*result, "rtt-buffers").status, CheckStatus::Failed);
}

TEST_F(Diagnose, LteTargetHasNoConsole) {
    auto session = open(options(false, "lte"), ConnectionState::ProbeOpen);
    ASSERT_TRUE(session);
    const OperationResult* result = finish(session->diagnose("lte"));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(statuses(*result),
              (std::vector<std::string>{"probe:ok", "power:ok", "connect:ok", "identify:ok",
                                        "rtt:skipped", "rtt-buffers:skipped"}));
    EXPECT_EQ(step(*result, "identify").result, "nRF9151, not protected");
}

TEST_F(Diagnose, CancelEndsWithCancelled) {
    target().set([](ScriptedTarget& t) { t.connectDelay = std::chrono::milliseconds(300); });
    auto session = open(options(false), ConnectionState::ProbeOpen);
    ASSERT_TRUE(session);
    const auto id = session->diagnose("app");
    ASSERT_TRUE(id);
    ASSERT_TRUE(waitFor([&] { return !steps().empty(); })); // the probe step finished
    ASSERT_TRUE(session->cancel(*id));
    const OperationResult* result = finish(id);
    ASSERT_NE(result, nullptr);
    ASSERT_FALSE(result->status);
    EXPECT_EQ(result->status.error().code, ErrorCode::Cancelled);
    EXPECT_EQ(result->checks.size(), 6U);
    EXPECT_EQ(step(*result, "rtt").status, CheckStatus::Skipped);
}

TEST_F(Diagnose, ConnectedSessionKeepsItsRttRunning) {
    auto session = open(options(true), ConnectionState::Connected);
    ASSERT_TRUE(session);
    const int starts = target().get([](ScriptedTarget& t) { return t.rttStarts; });
    const int stops = target().get([](ScriptedTarget& t) { return t.rttStops; });
    const OperationResult* result = finish(session->diagnose("app"));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(statuses(*result),
              (std::vector<std::string>{"probe:ok", "power:ok", "connect:ok", "identify:ok",
                                        "rtt:ok", "rtt-buffers:ok"}));
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.rttStarts; }), starts);
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.rttStops; }), stops);
    EXPECT_EQ(events().count(ConnectionState::Disconnected), 0);
    EXPECT_TRUE(events().reached(ConnectionState::Connected));
    // The console keeps working.
    target().output(0, "uart:~$ still here\n");
    EXPECT_TRUE(waitFor([&] { return !events().lines.empty(); }));
}

TEST_F(Diagnose, OtherTargetSwitchesAndRestores) {
    auto session = open(options(true), ConnectionState::Connected);
    ASSERT_TRUE(session);
    const int starts = target().get([](ScriptedTarget& t) { return t.rttStarts; });
    const auto id = session->diagnose("lte");
    const OperationResult* result = finish(id);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(step(*result, "identify").status, CheckStatus::Ok);
    EXPECT_EQ(step(*result, "rtt").status, CheckStatus::Skipped);
    const auto planned = hio::device::plannedPhases(OperationKind::Diagnose, true);
    EXPECT_EQ(events().phases(*id), std::vector<ProgramPhase>(planned.begin(), planned.end()));
    EXPECT_TRUE(waitFor(
        [&] { return target().get([](ScriptedTarget& t) { return t.rttStarts; }) > starts; }));
    EXPECT_TRUE(waitFor([&] { return events().reached(ConnectionState::Connected); }));
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.device; }), "NRF52840_xxAA");
}

// Review V10: the overall budget. A step that would start after it is Skipped with
// diag.check_timeout naming the step by its translated title (review V9), and so are the rest.
TEST_F(Diagnose, OverallBudgetSkipsTheRemainingSteps) {
    // The connect step (one attempt, 1.3 s) ends after the 1 s budget; the margin keeps the
    // earlier steps inside it also under the sanitizers.
    target().set([](ScriptedTarget& t) { t.connectDelay = std::chrono::milliseconds(1300); });
    setDiagnoseBudget(std::chrono::milliseconds(1000));
    auto session = open(options(false), ConnectionState::ProbeOpen);
    ASSERT_TRUE(session);
    const OperationResult* finished = finish(session->diagnose("app"));
    ASSERT_NE(finished, nullptr);
    const OperationResult& result = *finished;
    ASSERT_TRUE(result.status);
    EXPECT_EQ(statuses(result),
              (std::vector<std::string>{"probe:ok", "power:ok", "connect:ok", "identify:skipped",
                                        "rtt:skipped", "rtt-buffers:skipped"}));
    const DiagnoseStep& identify = step(result, "identify");
    ASSERT_TRUE(identify.error);
    EXPECT_EQ(codeOf(identify), ErrorCode::DiagCheckTimeout);
    EXPECT_EQ(identify.error.value_or(hio::Error{}).message,
              "The check \"MCU type\" did not finish in time.");
    EXPECT_EQ(identify.result, "Not checked: the time for the checks ran out");
    EXPECT_EQ(codeOf(step(result, "rtt-buffers")), ErrorCode::DiagCheckTimeout);
    EXPECT_EQ(step(result, "rtt-buffers").error.value_or(hio::Error{}).message,
              "The check \"Console channels\" did not finish in time.");
    EXPECT_TRUE(events().reached(ConnectionState::ProbeOpen));
}

// Review V6: Diagnose on a fatally stopped session that restores it to Connected clears the
// fatal state, so auto-reconnect works again afterwards.
TEST_F(Diagnose, DiagnoseOnAStoppedSessionRestoresAutoReconnect) {
    target().set([](ScriptedTarget& t) { t.openError = "Cannot open the USB device"; });
    SessionOptions opts = options(true);
    opts.autoReconnect = true;
    auto session = open(opts, ConnectionState::Stopped); // probe errors are fatal at first
    ASSERT_TRUE(session);
    target().set([](ScriptedTarget& t) { t.openError.reset(); });
    const OperationResult* result = finish(session->diagnose("app"));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(step(*result, "rtt-buffers").status, CheckStatus::Ok);
    ASSERT_TRUE(waitFor([&] { return events().reached(ConnectionState::Connected); }));
    // A power loss and its return: the session re-attaches by itself.
    target().set([](ScriptedTarget& t) { t.vtref = 0; });
    ASSERT_TRUE(waitFor([&] { return events().reached(ConnectionState::Disconnected); }));
    target().set([](ScriptedTarget& t) { t.vtref = 3300; });
    EXPECT_TRUE(waitFor([&] { return events().reached(ConnectionState::Connected); }));
}

TEST_F(Diagnose, BusyWhileAnotherOperationRuns) {
    target().set([](ScriptedTarget& t) { t.connectDelay = std::chrono::milliseconds(200); });
    auto session = open(options(false), ConnectionState::ProbeOpen);
    ASSERT_TRUE(session);
    const auto first = session->diagnose("app");
    ASSERT_TRUE(first);
    const auto second = session->diagnose("app");
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error().code, ErrorCode::InternalBusy);
    EXPECT_NE(finish(first), nullptr);
}

#ifdef Q_OS_LINUX
TEST_F(Diagnose, OpenFailureWithoutUsbAccessIsUsbAccessDenied) {
    if (::geteuid() == 0) {
        GTEST_SKIP() << "root can access every file";
    }
    const fs::path device = root() / "sys/bus/usb/devices/1-4";
    fs::create_directories(device);
    std::ofstream(device / "idVendor") << "1366\n";
    std::ofstream(device / "busnum") << "1\n";
    std::ofstream(device / "devnum") << "9\n";
    const fs::path node = root() / "dev/bus/usb/001/009";
    fs::create_directories(node.parent_path());
    std::ofstream(node) << "";
    fs::permissions(node, fs::perms::none);
    target().set([](ScriptedTarget& t) { t.openError = "Cannot open the USB device"; });
    auto session = open(options(false), ConnectionState::Stopped);
    ASSERT_TRUE(session);
    const auto& event = events().connections.back();
    ASSERT_TRUE(event.error);
    EXPECT_EQ(event.error.value_or(hio::Error{}).code, ErrorCode::UsbAccessDenied);
    EXPECT_NE(event.error.value_or(hio::Error{}).detail.find("/dev/bus/usb/001/009"),
              std::string::npos);
}

// Review V7: without access the library may not list the probe at all: probe.not_found with an
// inaccessible SEGGER device is usb.access_denied too.
TEST_F(Diagnose, NotFoundWithoutUsbAccessIsUsbAccessDenied) {
    if (::geteuid() == 0) {
        GTEST_SKIP() << "root can access every file";
    }
    const fs::path device = root() / "sys/bus/usb/devices/1-4";
    fs::create_directories(device);
    std::ofstream(device / "idVendor") << "1366\n";
    std::ofstream(device / "busnum") << "1\n";
    std::ofstream(device / "devnum") << "9\n";
    const fs::path node = root() / "dev/bus/usb/001/009";
    fs::create_directories(node.parent_path());
    std::ofstream(node) << "";
    fs::permissions(node, fs::perms::none);
    target().set([](ScriptedTarget& t) { t.probes.clear(); }); // not listed
    auto session = open(options(false), ConnectionState::Stopped);
    ASSERT_TRUE(session);
    const auto& event = events().connections.back();
    ASSERT_TRUE(event.error);
    EXPECT_EQ(event.error.value_or(hio::Error{}).code, ErrorCode::UsbAccessDenied);
    EXPECT_NE(event.error.value_or(hio::Error{}).detail.find("probe(s) listed"), std::string::npos);
}

TEST_F(Diagnose, NotFoundWithUsbAccessStaysNotFound) {
    target().set([](ScriptedTarget& t) { t.probes.clear(); });
    auto session = open(options(false), ConnectionState::Stopped);
    ASSERT_TRUE(session);
    const auto& event = events().connections.back();
    ASSERT_TRUE(event.error);
    EXPECT_EQ(event.error.value_or(hio::Error{}).code, ErrorCode::ProbeNotFound);
}

TEST_F(Diagnose, OpenFailureWithUsbAccessStaysOpenFailed) {
    target().set([](ScriptedTarget& t) { t.openError = "Cannot open the USB device"; });
    auto session = open(options(false), ConnectionState::Stopped);
    ASSERT_TRUE(session);
    const auto& event = events().connections.back();
    ASSERT_TRUE(event.error);
    EXPECT_EQ(event.error.value_or(hio::Error{}).code, ErrorCode::ProbeOpenFailed);
}
#endif

TEST(DiagnoseSteps, TitlesNameTheTarget) {
    const auto* chester = hio::device::ProfileRegistry::find("chester");
    ASSERT_NE(chester, nullptr);
    const auto steps = hio::device::diagnoseSteps(*hio::device::findTarget(*chester, "app"));
    ASSERT_EQ(steps.size(), hio::device::diagnoseStepIds.size());
    for (std::size_t i = 0; i < steps.size(); ++i) {
        EXPECT_EQ(steps[i].id, hio::device::diagnoseStepIds.at(i));
        EXPECT_FALSE(steps[i].title.empty());
    }
    EXPECT_EQ(steps[2].title, "Connection to the application MCU");
    EXPECT_EQ(hio::device::operationKindId(OperationKind::Diagnose), "diagnose");
}

TEST(ConnectionGuide, EveryTargetHasAGuideWithTexts) {
    for (const auto* profile : hio::device::ProfileRegistry::all()) {
        EXPECT_FALSE(profile->powerHintSourceText.empty()) << profile->id;
        for (const auto& target : profile->targets) {
            ASSERT_FALSE(target.connectionGuide.empty()) << target.id;
            for (const auto& step : target.connectionGuide) {
                EXPECT_FALSE(step.id.empty());
                EXPECT_FALSE(step.illustrationSlot.empty());
                EXPECT_FALSE(hio::device::guideStepTitle(step).isEmpty());
                EXPECT_FALSE(hio::device::guideStepBody(step).isEmpty());
            }
        }
    }
    const auto* chester = hio::device::ProfileRegistry::find("chester");
    EXPECT_EQ(hio::device::findTarget(*chester, "app")->connectionGuide.front().illustrationSlot,
              "chester-app-cable");
    EXPECT_EQ(hio::device::findTarget(*chester, "lte")->connectionGuide.front().illustrationSlot,
              "chester-lte-cable");
}

} // namespace
