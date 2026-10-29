// Hardware-in-the-loop tests with a CHESTER and a J-Link (plan/features/0002, phase G1).
// Skipped unless HWIO_HIL=1 (see CMakeLists.txt). The manual checks of phase G2 (power-cycle,
// reflash with JLinkExe while attached, outdated probe firmware) are listed in the plan.
// Plan 0021 phase 2: `info show` and the command tree (`help`, `<cmd> -h`) through silent shell
// queries on the real firmware (HWIO_HIL_SN as above).

#include "hwio/core/error.h"
#include "hwio/device/command_capture.h"
#include "hwio/device/command_tree.h"
#include "hwio/device/command_tree_cache.h"
#include "hwio/device/command_tree_learner.h"
#include "hwio/device/console_line.h"
#include "hwio/device/device_info.h"
#include "hwio/device/device_session.h"
#include "hwio/device/operation.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"
#include "hwio/device/session_registry.h"
#include "hwio/device/shell_query.h"
#include "hwio/hw/jlink/library_locator.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QList>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using hwio::device::Channel;
using hwio::device::ConnectionState;
using hwio::device::ConsoleLine;
using hwio::device::DeviceSession;
using hwio::device::Direction;

bool hilEnabled() { return qEnvironmentVariable("HWIO_HIL") == QStringLiteral("1"); }

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
            [this](const hwio::device::ConnectionEvent& event) { states.push_back(event.state); });
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
            GTEST_SKIP() << "hardware-in-the-loop test; set HWIO_HIL=1 to run it";
        }
        auto location = hwio::jlink::locateLibrary(hwio::jlink::defaultLocatorOptions({}, nullptr));
        ASSERT_TRUE(location) << location.error().detail;
        m_location = *location;
    }

    [[nodiscard]] static hwio::device::SessionOptions options(std::optional<std::uint32_t> sn,
                                                              bool autoReconnect = false) {
        hwio::device::SessionOptions options;
        options.profile = hwio::device::ProfileRegistry::find("chester");
        options.targetId = "app";
        options.probeSn = sn;
        options.speedKhz = 2000;
        options.latencyMs = 50;
        options.autoReconnect = autoReconnect;
        options.consoleFile = std::filesystem::path(); // none
        return options;
    }

    [[nodiscard]] const hwio::jlink::LibraryLocation& location() const { return m_location; }
    [[nodiscard]] hwio::device::SessionRegistry& registry() { return m_registry; }

private:
    hwio::jlink::LibraryLocation m_location;
    hwio::device::SessionRegistry m_registry;
};

TEST_F(Hil, ProbesAreListed) {
    const auto list = registry().enumerateProbes(location());
    ASSERT_TRUE(list) << list.error().detail;
    EXPECT_GE(list->libraryVersion, 80000U);
    EXPECT_FALSE(list->probes.empty());
}

TEST_F(Hil, ConsoleAttachesWithoutResetAndAnswersVersion) {
    auto session = DeviceSession::open(registry(), location(), options(serialFrom("HWIO_HIL_SN")));
    ASSERT_TRUE(session) << session.error().message;
    Recorder recorder(**session);
    const auto connected = hwio::device::waitUntilConnected(**session, std::chrono::seconds(20));
    ASSERT_TRUE(connected) << connected.error().message << " / " << connected.error().detail;
    ASSERT_TRUE((*session)->sendLine("version"));
    EXPECT_TRUE(waitFor([&] { return recorder.hasShellOutput(); }, std::chrono::seconds(5)));
}

TEST_F(Hil, ResetReattaches) {
    auto session = DeviceSession::open(registry(), location(), options(serialFrom("HWIO_HIL_SN")));
    ASSERT_TRUE(session);
    Recorder recorder(**session);
    ASSERT_TRUE(hwio::device::waitUntilConnected(**session, std::chrono::seconds(20)));
    recorder.lines.clear();
    ASSERT_TRUE((*session)->reset("app", hwio::device::ResetMode::Go));
    // The firmware boots again and prints its boot log.
    EXPECT_TRUE(waitFor([&] { return recorder.lines.size() >= 3; }, std::chrono::seconds(30)));
    EXPECT_EQ((*session)->connection().state, ConnectionState::Connected);
}

TEST_F(Hil, RebootFromTheShellIsReattachedAutomatically) {
    auto session =
        DeviceSession::open(registry(), location(), options(serialFrom("HWIO_HIL_SN"), true));
    ASSERT_TRUE(session);
    Recorder recorder(**session);
    ASSERT_TRUE(hwio::device::waitUntilConnected(**session, std::chrono::seconds(20)));
    recorder.lines.clear();
    ASSERT_TRUE((*session)->sendLine("kernel reboot cold"));
    EXPECT_TRUE(waitFor([&] { return recorder.lines.size() >= 3; }, std::chrono::seconds(60)));
    ASSERT_TRUE((*session)->sendLine("version"));
    EXPECT_TRUE(waitFor([&] { return recorder.hasShellOutput(); }, std::chrono::seconds(10)));
}

TEST_F(Hil, TwoChestersWithTwoProbesInOneProcess) {
    // HWIO_HIL_SN / HWIO_HIL_SN2, or the first two probes the library lists. Skipped with fewer
    // than two probes (deferred check of plan/features/0002, still required before M1 is Done).
    auto first = serialFrom("HWIO_HIL_SN");
    auto second = serialFrom("HWIO_HIL_SN2");
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
    ASSERT_TRUE(hwio::device::waitUntilConnected(**a, std::chrono::seconds(20)));
    ASSERT_TRUE(hwio::device::waitUntilConnected(**b, std::chrono::seconds(20)));
    ASSERT_TRUE((*a)->sendLine("version"));
    ASSERT_TRUE((*b)->sendLine("version"));
    EXPECT_TRUE(waitFor([&] { return ra.hasShellOutput() && rb.hasShellOutput(); },
                        std::chrono::seconds(10)));
}

TEST_F(Hil, InfoShowAndCommandTreeThroughSilentQueries) {
    auto session = DeviceSession::open(registry(), location(), options(serialFrom("HWIO_HIL_SN")));
    ASSERT_TRUE(session) << session.error().message;
    Recorder recorder(**session);
    ASSERT_TRUE(hwio::device::waitUntilConnected(**session, std::chrono::seconds(20)));
    recorder.lines.clear();

    auto info = hwio::device::ShellQuery::start(**session, "info show");
    ASSERT_TRUE(info) << info.error().message;
    const auto answered = hwio::device::waitForQuery(**info);
    ASSERT_TRUE(answered.has_value());
    const hwio::device::ShellQueryResult answer =
        answered.value_or(hwio::device::ShellQueryResult{});
    const hwio::device::DeviceInfo fields = hwio::device::parseInfoShow(answer.lines);
    EXPECT_TRUE(fields.hasFirmware());
    std::cout << "info show: " << fields.fwName() << " " << fields.fwVersion() << " ("
              << answer.lines.size() << " lines, end " << hwio::device::captureEndId(answer.end)
              << ")\n";

    QTemporaryDir dir;
    hwio::device::CommandTreeCache cache(std::filesystem::path(dir.path().toStdU16String()));
    hwio::device::ShellQueryQueue queue;
    queue.setSession(session->get());
    hwio::device::CommandTreeLearner learner(queue, &cache);
    learner.start(hwio::device::FirmwareKey{.profileId = "chester",
                                            .targetId = "app",
                                            .fwName = std::string(fields.fwName()),
                                            .fwVersion = std::string(fields.fwVersion())},
                  true);
    ASSERT_TRUE(waitFor([&] { return !learner.reading(); }, std::chrono::seconds(10)));
    const auto tree = learner.tree();
    ASSERT_TRUE(tree);
    ASSERT_FALSE(tree->empty());
    std::cout << "help: " << tree->root().children.size() << " commands\n";
    // Plan 0022 (ADR-0178): CHESTER's `help` shows the CONFIG_SHELL_HELP marker, else no `-h`.
    ASSERT_TRUE(tree->dashHSupported()) << "no `<-h> or <--help>` line in the answer to `help`";
    const std::string first = tree->root().children.front().name;
    learner.requestChildren({first});
    ASSERT_TRUE(waitFor([&] { return !learner.reading(); }, std::chrono::seconds(10)));
    const auto* node = learner.tree()->find(std::vector<std::string>{first});
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(node->childrenKnown);
    std::cout << first << " -h: " << node->children.size() << " subcommands\n";

    // Silent: none of the queries reached the console.
    for (const ConsoleLine& line : recorder.lines) {
        EXPECT_FALSE(
            line.dir == Direction::In &&
            (line.text == "info show" || line.text == "help" || line.text == first + " -h"))
            << line.text;
    }
    // A user command afterwards works as before.
    recorder.lines.clear();
    ASSERT_TRUE((*session)->sendLine("version"));
    EXPECT_TRUE(waitFor([&] { return recorder.hasShellOutput(); }, std::chrono::seconds(5)));
}

} // namespace
