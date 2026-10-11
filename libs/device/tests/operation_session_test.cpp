// DeviceSession operations (plan/features/0003, C4; ADR-0057, ADR-0062) with the in-process
// scripted J-Link and an in-process modem updater. Run under ASan/UBSan and TSan by the presets.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/device_session.h"
#include "hio/device/operation.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/device/session_registry.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/lte_input.h"
#include "hio/firmware/program_progress.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/programmer.h"
#include "hio/hw/nrfutil/nrfutil.h"
#include "scripted_api.h"
#include "session_internal.h"
#include "session_test_support.h"

#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hio::ErrorCode;
using hio::Status;
using hio::device::ConnectionState;
using hio::device::DeviceSession;
using hio::device::EraseScope;
using hio::device::FlashOptions;
using hio::device::OperationId;
using hio::device::OperationKind;
using hio::device::OperationResult;
using hio::device::ResetMode;
using hio::device::SessionOptions;
using hio::device::SessionRegistry;
using hio::device::detail::OpenHooks;
using hio::device::test::Recorder;
using hio::device::test::ScriptedApi;
using hio::device::test::ScriptedTarget;
using hio::device::test::waitFor;
using hio::firmware::FirmwareImage;
using hio::firmware::ProgramPhase;
using hio::firmware::Segment;

// What the in-process modem updater does.
struct ModemScript {
    std::mutex mutex;
    enum class Mode : std::uint8_t { Succeed, Fail, WaitForStop } mode = Mode::Succeed;
    int calls = 0;
    std::optional<bool> probeOpenDuringUpdate;
    std::uint32_t serial = 0;
    bool factoryFails = false;
};

class ScriptedModemUpdater final : public hio::nrfutil::IModemUpdater {
public:
    ScriptedModemUpdater(std::shared_ptr<ModemScript> script,
                         std::shared_ptr<ScriptedTarget> target)
        : m_script(std::move(script)), m_target(std::move(target)) {}

    Status updateModem(const fs::path& /*zip*/, std::uint32_t probeSn,
                       const hio::firmware::ProgressFn& progress, std::stop_token stop) override {
        ModemScript::Mode mode{};
        {
            const std::scoped_lock lock(m_script->mutex);
            ++m_script->calls;
            m_script->serial = probeSn;
            m_script->probeOpenDuringUpdate =
                m_target->get([](ScriptedTarget& t) { return t.open; });
            mode = m_script->mode;
        }
        if (progress) {
            progress({.phase = ProgramPhase::Modem, .done = 50, .total = 100});
        }
        if (mode == ModemScript::Mode::WaitForStop) {
            while (!stop.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return hio::fail(ErrorCode::Cancelled);
        }
        if (mode == ModemScript::Mode::Fail) {
            return std::unexpected(
                hio::makeError(ErrorCode::FirmwareModemUpdateFailed, "simulated"));
        }
        return {};
    }

private:
    std::shared_ptr<ModemScript> m_script;
    std::shared_ptr<ScriptedTarget> m_target;
};

FirmwareImage appImage(std::size_t size = 2048, std::uint8_t value = 0x5A) {
    std::vector<Segment> segments;
    segments.push_back(
        Segment{.address = 0x1000, .data = std::vector<std::byte>(size, std::byte{value})});
    return *FirmwareImage::fromSegments(std::move(segments));
}

class Operations : public ::testing::Test {
protected:
    Operations()
        : m_registry(std::make_unique<SessionRegistry>(
              hio::device::RegistryOptions{.instanceRoot = root() / "instances",
                                           .lockDirectory = root() / "locks",
                                           .consoleDirectory = root() / "console"})) {}

    [[nodiscard]] fs::path root() const { return {m_dir.path().toStdString()}; }

    [[nodiscard]] static SessionOptions options(std::string_view target = "app",
                                                bool attachRtt = true) {
        SessionOptions options;
        options.profile = hio::device::ProfileRegistry::find("chester");
        options.targetId = target;
        options.speedKhz = 2000;
        options.latencyMs = 5;
        options.attachRtt = attachRtt;
        options.consoleFile = fs::path();
        return options;
    }

    [[nodiscard]] OpenHooks hooks() {
        OpenHooks hooks;
        hooks.apiFactory = [target =
                                m_target]() -> hio::Result<std::unique_ptr<hio::jlink::IJLinkApi>> {
            return std::make_unique<ScriptedApi>(target);
        };
        hooks.modemUpdaterFactory =
            [script = m_modem,
             target = m_target]() -> hio::Result<std::unique_ptr<hio::nrfutil::IModemUpdater>> {
            {
                const std::scoped_lock lock(script->mutex);
                if (script->factoryFails) {
                    return std::unexpected(hio::makeError(ErrorCode::DependencyNrfUtilNotFound));
                }
            }
            return std::make_unique<ScriptedModemUpdater>(script, target);
        };
        hooks.timing.powerCheckInterval = std::chrono::milliseconds(20);
        hooks.timing.reconnectInterval = std::chrono::milliseconds(60);
        hooks.timing.probe.connectRetryDelay = std::chrono::milliseconds(5);
        hooks.timing.rtt.attachDeadline = std::chrono::milliseconds(5000);
        hooks.timing.rtt.pollInterval = std::chrono::milliseconds(5);
        hooks.timing.rtt.loggerWait = std::chrono::milliseconds(50);
        hooks.timing.rtt.staleRetryDelay = std::chrono::milliseconds(5);
        hooks.timing.rtt.writeTimeout = std::chrono::milliseconds(100);
        hooks.timing.programmer.recoverPoll = std::chrono::milliseconds(1);
        return hooks;
    }

    std::unique_ptr<DeviceSession> openConnected(const SessionOptions& opts) {
        auto session = hio::device::detail::openSession(*m_registry, hio::jlink::LibraryLocation{},
                                                        opts, hooks());
        EXPECT_TRUE(session) << session.error().message;
        if (!session) {
            return nullptr;
        }
        m_events = std::make_unique<Recorder>(**session);
        EXPECT_TRUE(waitFor([&] { return m_events->reached(ConnectionState::Connected); }));
        return std::move(*session);
    }

    // Waits for the result of `id`.
    const OperationResult* finish(const hio::Result<OperationId>& id) {
        EXPECT_TRUE(id) << (id ? std::string() : id.error().message);
        if (!id) {
            return nullptr;
        }
        EXPECT_TRUE(waitFor([&] { return events().result(*id) != nullptr; }));
        return events().result(*id);
    }

    [[nodiscard]] Recorder& events() { return *m_events; }
    [[nodiscard]] ScriptedTarget& target() { return *m_target; }
    [[nodiscard]] ModemScript& modem() { return *m_modem; }
    [[nodiscard]] std::vector<std::string> ops() {
        return target().get([](ScriptedTarget& t) { return t.ops; });
    }
    void clearOps() {
        target().set([](ScriptedTarget& t) { t.ops.clear(); });
    }
    [[nodiscard]] int rttStarts() {
        return target().get([](ScriptedTarget& t) { return t.rttStarts; });
    }
    [[nodiscard]] std::shared_ptr<const hio::firmware::LteBundle> bundle() const {
        auto result = std::make_shared<hio::firmware::LteBundle>();
        result->modemZip = root() / "modem.zip";
        result->application = appImage();
        return result;
    }

    static void expectPlanned(const Recorder& recorder, OperationId id, OperationKind kind,
                              bool cross, bool image = false) {
        const auto planned = hio::device::plannedPhases(kind, cross, image);
        EXPECT_EQ(recorder.phases(id), std::vector<ProgramPhase>(planned.begin(), planned.end()))
            << hio::device::operationKindId(kind);
    }

private:
    QTemporaryDir m_dir;
    std::shared_ptr<ScriptedTarget> m_target = std::make_shared<ScriptedTarget>();
    std::shared_ptr<ModemScript> m_modem = std::make_shared<ModemScript>();
    std::unique_ptr<SessionRegistry> m_registry;
    std::unique_ptr<Recorder> m_events;
};

TEST_F(Operations, FlashWithRttPausesAndReattaches) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    const int starts = rttStarts();
    const auto stops = target().get([](ScriptedTarget& t) { return t.rttStops; });
    const auto id = session->flash("app", std::make_shared<FirmwareImage>(appImage()), {});
    const OperationResult* result = finish(id);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->status) << result->status.error().detail;
    ASSERT_TRUE(result->chip);
    EXPECT_EQ(result->chip.value_or(hio::jlink::ChipInfo{}).name, "nRF52840");
    expectPlanned(events(), *id, OperationKind::Flash, false);
    EXPECT_GT(target().get([](ScriptedTarget& t) { return t.rttStops; }), stops);
    EXPECT_TRUE(waitFor([&] { return rttStarts() > starts; }));
    EXPECT_TRUE(events().reached(ConnectionState::Connected));
    EXPECT_EQ(events().count(ConnectionState::Disconnected), 0); // RTT paused silently
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.app.flash.at(0x1000); }), 0x5A);
    const auto sequence = ops();
    EXPECT_NE(std::ranges::find(sequence, "download"), sequence.end());
    EXPECT_EQ(sequence.back(), "go"); // reset(Go) last
    EXPECT_FALSE(session->runningOperation());
}

TEST_F(Operations, FlashHaltLeavesTheSessionDisconnected) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    const OperationResult* result =
        finish(session->flash("app", std::make_shared<FirmwareImage>(appImage()),
                              FlashOptions{.halt = true, .allowUicr = false}));
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->status);
    EXPECT_TRUE(events().reached(ConnectionState::Disconnected));
    EXPECT_FALSE(events().connections.back().error.has_value());
}

TEST_F(Operations, OneOperationAtATime) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    target().set([](ScriptedTarget& t) { t.endDownloadDelay = std::chrono::milliseconds(300); });
    const auto first = session->flash("app", std::make_shared<FirmwareImage>(appImage()), {});
    ASSERT_TRUE(first);
    const auto second = session->reset("app", ResetMode::Go);
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error().code, ErrorCode::InternalBusy);
    ASSERT_NE(finish(first), nullptr);
    EXPECT_TRUE(finish(session->reset("app", ResetMode::Go))->status);
}

TEST_F(Operations, InvalidRequests) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    EXPECT_EQ(session->reset("ble", ResetMode::Go).error().code, ErrorCode::UsageInvalidArgument);
    EXPECT_EQ(session->readMemory("app", 0, 0).error().code, ErrorCode::UsageInvalidArgument);
    EXPECT_EQ(session->readMemory("app", 0, (64ULL * 1024) + 1).error().code,
              ErrorCode::UsageInvalidArgument);
    EXPECT_EQ(session->flash("app", std::make_shared<FirmwareImage>(), {}).error().code,
              ErrorCode::UsageInvalidArgument);
    EXPECT_EQ(session->cancel(42).error().code, ErrorCode::UsageInvalidArgument);
}

TEST_F(Operations, CancelDuringVerify) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    target().set([](ScriptedTarget& t) { t.readDelay = std::chrono::milliseconds(50); });
    // 64 KiB = 4 verify chunks of 16 KiB.
    const auto id =
        session->flash("app", std::make_shared<FirmwareImage>(appImage(64ULL * 1024)), {});
    ASSERT_TRUE(id);
    ASSERT_TRUE(waitFor([&] {
        return std::ranges::any_of(events().progress, [](const auto& p) {
            return p.second.phase == ProgramPhase::Verifying;
        });
    }));
    ASSERT_TRUE(session->cancel(*id));
    const OperationResult* result = finish(id);
    ASSERT_NE(result, nullptr);
    ASSERT_FALSE(result->status);
    EXPECT_EQ(result->status.error().code, ErrorCode::Cancelled);
    // Let the CPU run again and re-attach the console.
    EXPECT_EQ(ops().back(), "go");
    EXPECT_TRUE(waitFor([&] { return events().reached(ConnectionState::Connected); }));
}

TEST_F(Operations, CloseDuringAnOperationJoinsPromptly) {
    auto session = openConnected(options());
    ASSERT_TRUE(session);
    target().set([](ScriptedTarget& t) { t.readDelay = std::chrono::milliseconds(20); });
    const auto id =
        session->flash("app", std::make_shared<FirmwareImage>(appImage(512ULL * 1024)), {});
    ASSERT_TRUE(id);
    ASSERT_TRUE(waitFor(
        [&] { return target().get([](ScriptedTarget& t) { return t.readMemoryCalls; }) > 0; }));
    const auto started = std::chrono::steady_clock::now();
    session->close();
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(2));
}

TEST_F(Operations, CrossTargetFlashSwitchesAndRestores) {
    auto session = openConnected(options("app"));
    ASSERT_TRUE(session);
    clearOps();
    const int starts = rttStarts();
    const auto id = session->flash("lte", std::make_shared<FirmwareImage>(appImage()), {});
    const OperationResult* result = finish(id);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->status) << result->status.error().detail;
    EXPECT_EQ(result->chip.value_or(hio::jlink::ChipInfo{}).name, "nRF9151");
    expectPlanned(events(), *id, OperationKind::Flash, true);
    const auto sequence = ops();
    const auto lte = std::ranges::find(sequence, "connect:NRF9151_XXCA");
    const auto app = std::ranges::find(sequence, "connect:NRF52840_xxAA");
    ASSERT_NE(lte, sequence.end());
    ASSERT_NE(app, sequence.end());
    EXPECT_LT(lte, app); // switched to lte, restored to app
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.lte.flash.at(0x1000); }), 0x5A);
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.app.flash.at(0x1000); }), 0xFF);
    EXPECT_TRUE(waitFor([&] { return rttStarts() > starts; })); // RTT back on app
    EXPECT_TRUE(events().reached(ConnectionState::Connected));
}

TEST_F(Operations, CrossTargetRestoresAfterFailureAndCancel) {
    auto session = openConnected(options("app"));
    ASSERT_TRUE(session);
    target().set([](ScriptedTarget& t) { t.endDownloadResult = -5; });
    int starts = rttStarts();
    const OperationResult* failed =
        finish(session->flash("lte", std::make_shared<FirmwareImage>(appImage()), {}));
    ASSERT_NE(failed, nullptr);
    ASSERT_FALSE(failed->status);
    EXPECT_EQ(failed->status.error().code, ErrorCode::TargetProgramFailed);
    EXPECT_EQ(failed->status.error().message, "Programming the lte MCU failed.");
    EXPECT_TRUE(waitFor(
        [&] { return rttStarts() > starts && events().reached(ConnectionState::Connected); }));

    target().set([](ScriptedTarget& t) {
        t.endDownloadResult = 0;
        t.readDelay = std::chrono::milliseconds(50);
    });
    starts = rttStarts();
    const auto id =
        session->flash("lte", std::make_shared<FirmwareImage>(appImage(64ULL * 1024)), {});
    ASSERT_TRUE(id);
    ASSERT_TRUE(waitFor([&] {
        return std::ranges::any_of(events().progress, [&](const auto& p) {
            return p.first == *id && p.second.phase == ProgramPhase::Verifying;
        });
    }));
    ASSERT_TRUE(session->cancel(*id));
    const OperationResult* cancelled = finish(id);
    ASSERT_NE(cancelled, nullptr);
    EXPECT_EQ(cancelled->status.error().code, ErrorCode::Cancelled);
    EXPECT_EQ(events().phases(*id).back(), ProgramPhase::Restoring);
    EXPECT_TRUE(waitFor([&] { return rttStarts() > starts; }));
}

TEST_F(Operations, FailedRestoreReportsDisconnected) {
    auto session = openConnected(options("app"));
    ASSERT_TRUE(session);
    // The app MCU cannot be reached again after the switch to lte.
    target().set([](ScriptedTarget& t) { t.failConnectTo = "NRF52840_xxAA"; });
    const OperationResult* result = finish(session->reset("lte", ResetMode::Go));
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->status); // the reset itself worked
    ASSERT_TRUE(waitFor([&] { return events().reached(ConnectionState::Disconnected); }));
    ASSERT_TRUE(events().connections.back().error.has_value());
    EXPECT_EQ(events().connections.back().error.value_or(hio::Error{}).code,
              ErrorCode::TargetConnectFailed);
}

TEST_F(Operations, EveryKindOnAFlashOnlySession) {
    auto session = openConnected(options("lte", false));
    ASSERT_TRUE(session);
    EXPECT_EQ(rttStarts(), 0); // attachRtt = false
    const auto flash = session->flash("lte", std::make_shared<FirmwareImage>(appImage()), {});
    ASSERT_TRUE(finish(flash)->status);
    expectPlanned(events(), *flash, OperationKind::Flash, false);

    const auto eraseCode = session->erase("lte", EraseScope::Code);
    ASSERT_TRUE(finish(eraseCode)->status);
    expectPlanned(events(), *eraseCode, OperationKind::EraseCode, false);
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.lte.flash.at(0x1000); }), 0xFF);

    target().set([](ScriptedTarget& t) { t.lte.uicr.at(0x108) = 0xBA; });
    const auto eraseAll = session->erase("lte", EraseScope::All);
    ASSERT_TRUE(finish(eraseAll)->status);
    expectPlanned(events(), *eraseAll, OperationKind::EraseAll, false);
    EXPECT_EQ(target().get([](ScriptedTarget& t) { return t.lte.uicr.at(0x108); }), 0xFF);

    const auto recover = session->recover("lte");
    const OperationResult* recovered = finish(recover);
    ASSERT_TRUE(recovered->status) << recovered->status.error().detail;
    expectPlanned(events(), *recover, OperationKind::Recover, false, true);
    // The disable-AP-protect image was programmed.
    EXPECT_NE(target().get([](ScriptedTarget& t) { return t.lte.flash.at(0); }), 0xFF);

    const auto reset = session->reset("lte", ResetMode::Halt);
    ASSERT_TRUE(finish(reset)->status);
    expectPlanned(events(), *reset, OperationKind::Reset, false);

    target().set([](ScriptedTarget& t) {
        for (std::size_t i = 0; i < 4; ++i) {
            t.lte.uicr.at(0x108 + i) = static_cast<std::uint8_t>(0xB0 + i);
        }
    });
    const auto read = session->readMemory("lte", 0x00FF8108, 4);
    const OperationResult* data = finish(read);
    ASSERT_TRUE(data->status);
    ASSERT_EQ(data->data.size(), 4U);
    EXPECT_EQ(data->data[3], std::byte{0xB3});
    expectPlanned(events(), *read, OperationKind::ReadMemory, false);

    const auto update = session->updateModem("lte", root() / "mfw.zip");
    ASSERT_TRUE(finish(update)->status);
    expectPlanned(events(), *update, OperationKind::Modem, false);
    EXPECT_EQ(modem().probeOpenDuringUpdate, false); // released for nRF Util
    EXPECT_EQ(modem().serial, 801000001U);
    EXPECT_TRUE(target().get([](ScriptedTarget& t) { return t.open; })); // opened again

    const auto lteBundle = session->flashLteBundle("lte", bundle(), {});
    ASSERT_TRUE(finish(lteBundle)->status);
    expectPlanned(events(), *lteBundle, OperationKind::LteBundle, false);
    EXPECT_TRUE(events().reached(ConnectionState::Connected));
}

TEST_F(Operations, ProtectedFlashFailsAndRecoverUnlocks) {
    auto session = openConnected(options("app", false));
    ASSERT_TRUE(session);
    target().set([](ScriptedTarget& t) { t.app.protectedChip = true; });
    // The connection drops (the chip locked itself): the operation reconnects and finds APPROTECT.
    target().set([](ScriptedTarget& t) { t.connected = false; });
    const OperationResult* flash =
        finish(session->flash("app", std::make_shared<FirmwareImage>(appImage()), {}));
    ASSERT_NE(flash, nullptr);
    ASSERT_FALSE(flash->status);
    EXPECT_EQ(flash->status.error().code, ErrorCode::TargetProtected);
    EXPECT_EQ(flash->status.error().message, "The app MCU is read-back protected.");
    const OperationResult* recover = finish(session->recover("app"));
    ASSERT_TRUE(recover->status) << recover->status.error().detail;
    EXPECT_FALSE(target().get([](ScriptedTarget& t) { return t.app.protectedChip; }));
    EXPECT_TRUE(
        finish(session->flash("app", std::make_shared<FirmwareImage>(appImage()), {}))->status);
}

TEST_F(Operations, LteBundleStepsAndModemFailureSkipsTheApplication) {
    auto session = openConnected(options("lte", false));
    ASSERT_TRUE(session);
    const auto ok = session->flashLteBundle("lte", bundle(), {});
    ASSERT_TRUE(finish(ok)->status);
    int maxStep = 0;
    for (const auto& [operation, progress] : events().progress) {
        if (operation == *ok) {
            EXPECT_EQ(progress.steps, 2);
            EXPECT_EQ(progress.step, progress.phase == ProgramPhase::Modem ? 1 : 2);
            maxStep = std::max(maxStep, progress.step);
        }
    }
    EXPECT_EQ(maxStep, 2);

    clearOps();
    modem().mode = ModemScript::Mode::Fail;
    const OperationResult* failed = finish(session->flashLteBundle("lte", bundle(), {}));
    ASSERT_FALSE(failed->status);
    EXPECT_EQ(failed->status.error().code, ErrorCode::FirmwareModemUpdateFailed);
    const auto sequence = ops();
    EXPECT_EQ(std::ranges::find(sequence, "download"), sequence.end()); // step 2 skipped
}

TEST_F(Operations, LteBundleCancelInEachStep) {
    auto session = openConnected(options("lte", false));
    ASSERT_TRUE(session);
    modem().mode = ModemScript::Mode::WaitForStop;
    const auto first = session->flashLteBundle("lte", bundle(), {});
    ASSERT_TRUE(first);
    ASSERT_TRUE(waitFor([&] {
        const std::scoped_lock lock(modem().mutex);
        return modem().calls == 1;
    }));
    ASSERT_TRUE(session->cancel(*first));
    const OperationResult* cancelled = finish(first);
    EXPECT_EQ(cancelled->status.error().code, ErrorCode::Cancelled);
    EXPECT_TRUE(target().get([](ScriptedTarget& t) { return t.open; })); // reopened

    modem().mode = ModemScript::Mode::Succeed;
    target().set([](ScriptedTarget& t) { t.readDelay = std::chrono::milliseconds(50); });
    auto big = std::make_shared<hio::firmware::LteBundle>(*bundle());
    big->application = appImage(64ULL * 1024);
    const auto second = session->flashLteBundle("lte", big, {});
    ASSERT_TRUE(second);
    ASSERT_TRUE(waitFor([&] {
        return std::ranges::any_of(events().progress, [&](const auto& p) {
            return p.first == *second && p.second.phase == ProgramPhase::Verifying;
        });
    }));
    ASSERT_TRUE(session->cancel(*second));
    EXPECT_EQ(finish(second)->status.error().code, ErrorCode::Cancelled);
}

TEST_F(Operations, CrossTargetModemAndBundle) {
    auto session = openConnected(options("app"));
    ASSERT_TRUE(session);
    const auto update = session->updateModem("lte", root() / "mfw.zip");
    ASSERT_TRUE(finish(update)->status);
    expectPlanned(events(), *update, OperationKind::Modem, true);
    const auto lteBundle = session->flashLteBundle("lte", bundle(), {});
    ASSERT_TRUE(finish(lteBundle)->status);
    expectPlanned(events(), *lteBundle, OperationKind::LteBundle, true);
    EXPECT_TRUE(waitFor([&] { return events().reached(ConnectionState::Connected); }));
}

TEST_F(Operations, NrfUtilMissingFailsBeforeReleasingTheProbe) {
    auto session = openConnected(options("lte", false));
    ASSERT_TRUE(session);
    modem().factoryFails = true;
    const OperationResult* result = finish(session->updateModem("lte", root() / "mfw.zip"));
    ASSERT_FALSE(result->status);
    EXPECT_EQ(result->status.error().code, ErrorCode::DependencyNrfUtilNotFound);
    const auto sequence = ops();
    EXPECT_EQ(std::ranges::find(sequence, "close"), sequence.end());
}

TEST_F(Operations, WrongFamilyOnTheConsoleTarget) {
    // The probe reaches an nRF91 although the session is on app (wrong connector).
    target().set([](ScriptedTarget& t) { t.app = hio::device::test::nrf91Chip(); });
    auto session = openConnected(options("app", false));
    ASSERT_TRUE(session);
    const OperationResult* result = finish(session->erase("app", EraseScope::Code));
    ASSERT_FALSE(result->status);
    EXPECT_EQ(result->status.error().code, ErrorCode::TargetWrongFamily);
}

} // namespace
