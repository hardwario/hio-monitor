// Hardware-in-the-loop tests with a CHESTER and a J-Link (plan/features/0002, phase G1).
// Skipped unless HIO_HIL=1 (see CMakeLists.txt). The manual checks of phase G2 (power-cycle,
// reflash with JLinkExe while attached, outdated probe firmware) are listed in the plan.

#include "hio/core/error.h"
#include "hio/device/console_line.h"
#include "hio/device/device_session.h"
#include "hio/device/operation.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/device/session_registry.h"
#include "hio/hw/jlink/library_locator.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringLiteral>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using hio::device::Channel;
using hio::device::ConnectionState;
using hio::device::ConsoleLine;
using hio::device::DeviceSession;
using hio::device::Direction;

bool hilEnabled() { return qEnvironmentVariable("HIO_HIL") == QStringLiteral("1"); }

std::optional<std::uint32_t> serialFrom(const char* variable) {
    bool ok = false;
    const std::uint32_t value = qEnvironmentVariable(variable).toUInt(&ok);
    return ok ? std::optional(value) : std::nullopt;
}

bool waitFor(const std::function<bool()>& done, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

struct Recorder {
    explicit Recorder(DeviceSession& session) {
        QObject::connect(&session, &DeviceSession::linesReceived, &context,
                         [this](const QList<ConsoleLine>& received) {
                             for (const ConsoleLine& line : received) {
                                 lines.push_back(line);
                             }
                         });
        QObject::connect(
            &session, &DeviceSession::connectionChanged, &context,
            [this](const hio::device::ConnectionEvent& event) { states.push_back(event.state); });
    }

    [[nodiscard]] bool hasShellOutput() const {
        return std::ranges::any_of(lines, [](const ConsoleLine& line) {
            return line.channel == Channel::Shell && line.dir == Direction::Out;
        });
    }

    std::vector<ConsoleLine> lines;
    std::vector<ConnectionState> states;
    QObject context;
};

class Hil : public ::testing::Test {
protected:
    void SetUp() override {
        if (!hilEnabled()) {
            GTEST_SKIP() << "hardware-in-the-loop test; set HIO_HIL=1 to run it";
        }
        auto location = hio::jlink::locateLibrary(hio::jlink::defaultLocatorOptions({}, nullptr));
        ASSERT_TRUE(location) << location.error().detail;
        m_location = *location;
    }

    [[nodiscard]] static hio::device::SessionOptions options(std::optional<std::uint32_t> sn,
                                                             bool autoReconnect = false) {
        hio::device::SessionOptions options;
        options.profile = hio::device::ProfileRegistry::find("chester");
        options.targetId = "app";
        options.probeSn = sn;
        options.speedKhz = 2000;
        options.latencyMs = 50;
        options.autoReconnect = autoReconnect;
        options.consoleFile = std::filesystem::path(); // none
        return options;
    }

    [[nodiscard]] const hio::jlink::LibraryLocation& location() const { return m_location; }
    [[nodiscard]] hio::device::SessionRegistry& registry() { return m_registry; }

private:
    hio::jlink::LibraryLocation m_location;
    hio::device::SessionRegistry m_registry;
};

TEST_F(Hil, ProbesAreListed) {
    const auto list = registry().enumerateProbes(location());
    ASSERT_TRUE(list) << list.error().detail;
    EXPECT_GE(list->libraryVersion, 80000U);
    EXPECT_FALSE(list->probes.empty());
}

TEST_F(Hil, ConsoleAttachesWithoutResetAndAnswersVersion) {
    auto session = DeviceSession::open(registry(), location(), options(serialFrom("HIO_HIL_SN")));
    ASSERT_TRUE(session) << session.error().message;
    Recorder recorder(**session);
    const auto connected = hio::device::waitUntilConnected(**session, std::chrono::seconds(20));
    ASSERT_TRUE(connected) << connected.error().message << " / " << connected.error().detail;
    ASSERT_TRUE((*session)->sendLine("version"));
    EXPECT_TRUE(waitFor([&] { return recorder.hasShellOutput(); }, std::chrono::seconds(5)));
}

TEST_F(Hil, ResetReattaches) {
    auto session = DeviceSession::open(registry(), location(), options(serialFrom("HIO_HIL_SN")));
    ASSERT_TRUE(session);
    Recorder recorder(**session);
    ASSERT_TRUE(hio::device::waitUntilConnected(**session, std::chrono::seconds(20)));
    recorder.lines.clear();
    ASSERT_TRUE((*session)->reset("app", hio::device::ResetMode::Go));
    // The firmware boots again and prints its boot log.
    EXPECT_TRUE(waitFor([&] { return recorder.lines.size() >= 3; }, std::chrono::seconds(30)));
    EXPECT_EQ((*session)->connection().state, ConnectionState::Connected);
}

TEST_F(Hil, RebootFromTheShellIsReattachedAutomatically) {
    auto session =
        DeviceSession::open(registry(), location(), options(serialFrom("HIO_HIL_SN"), true));
    ASSERT_TRUE(session);
    Recorder recorder(**session);
    ASSERT_TRUE(hio::device::waitUntilConnected(**session, std::chrono::seconds(20)));
    recorder.lines.clear();
    ASSERT_TRUE((*session)->sendLine("kernel reboot cold"));
    EXPECT_TRUE(waitFor([&] { return recorder.lines.size() >= 3; }, std::chrono::seconds(60)));
    ASSERT_TRUE((*session)->sendLine("version"));
    EXPECT_TRUE(waitFor([&] { return recorder.hasShellOutput(); }, std::chrono::seconds(10)));
}

TEST_F(Hil, TwoChestersWithTwoProbesInOneProcess) {
    // HIO_HIL_SN / HIO_HIL_SN2, or the first two probes the library lists. Skipped with fewer than
    // two probes (deferred check of plan/features/0002, still required before M1 is Done).
    auto first = serialFrom("HIO_HIL_SN");
    auto second = serialFrom("HIO_HIL_SN2");
    if (!first || !second) {
        const auto list = registry().enumerateProbes(location());
        ASSERT_TRUE(list) << list.error().detail;
        if (list->probes.size() < 2) {
            GTEST_SKIP() << "needs two J-Link probes with a CHESTER each";
        }
        first = list->probes[0].probe.serialNumber;
        second = list->probes[1].probe.serialNumber;
    }
    auto a = DeviceSession::open(registry(), location(), options(first));
    auto b = DeviceSession::open(registry(), location(), options(second));
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    Recorder ra(**a);
    Recorder rb(**b);
    ASSERT_TRUE(hio::device::waitUntilConnected(**a, std::chrono::seconds(20)));
    ASSERT_TRUE(hio::device::waitUntilConnected(**b, std::chrono::seconds(20)));
    ASSERT_TRUE((*a)->sendLine("version"));
    ASSERT_TRUE((*b)->sendLine("version"));
    EXPECT_TRUE(waitFor([&] { return ra.hasShellOutput() && rb.hasShellOutput(); },
                        std::chrono::seconds(10)));
}

} // namespace
