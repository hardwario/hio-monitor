// Plan 0021 phase 2 (ADR-0175): CommandCapture (the ADR-0104 end rule: prompt, silence,
// timeout, echo filtering, bound) and ShellQuery (silent: not in the log, user lines held, no
// query while busy or not connected) against an in-process ConsoleSession.

#include "hwio/core/error.h"
#include "hwio/core/result.h"
#include "hwio/device/command_capture.h"
#include "hwio/device/command_tree.h"
#include "hwio/device/command_tree_cache.h"
#include "hwio/device/command_tree_learner.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_log.h"
#include "hwio/device/console_session.h"
#include "hwio/device/device_info_watcher.h"
#include "hwio/device/firmware_identity.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_events.h"
#include "hwio/device/shell_query.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QList>
#include <QMessageLogContext>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QtCore/qtmetamacros.h>
#include <QtLogging>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using hwio::device::CaptureEnd;
using hwio::device::CaptureOptions;
using hwio::device::Channel;
using hwio::device::CommandCapture;
using hwio::device::ConsoleLine;
using hwio::device::Direction;
using hwio::device::ShellQuery;
using hwio::device::ShellQueryQueue;
using hwio::device::ShellQueryResult;

// CHESTER with an RTT prompt on the app target (CHESTER's own is empty: the silence rule).
const hwio::device::DeviceProfile& promptProfile() {
    static const std::array<hwio::device::TargetProfile, 2> targets = [] {
        const auto* chester = hwio::device::ProfileRegistry::find("chester");
        std::array<hwio::device::TargetProfile, 2> copy{chester->targets[0], chester->targets[1]};
        copy[0].shellPrompt = "uart:~$ ";
        return copy;
    }();
    static const hwio::device::DeviceProfile profile = [] {
        hwio::device::DeviceProfile copy = *hwio::device::ProfileRegistry::find("chester");
        copy.targets = targets;
        return copy;
    }();
    return profile;
}

// Delivers lines like the real sessions: offered to a shell capture first, else appended to the
// log and emitted; sendLine() honours the hold and echoes the written line (dir In).
class FakeSession final : public hwio::device::ConsoleSession {
public:
    explicit FakeSession(const hwio::device::DeviceProfile& profile =
                             *hwio::device::ProfileRegistry::find("chester"))
        : m_profile(&profile) {
        m_connection.state = hwio::device::ConnectionState::Connected;
    }

    hwio::Status sendLine(std::string line) override {
        if (hwio::Status held; holdLine(line, held)) {
            return held;
        }
        m_written.push_back(line);
        lineQueued();
        if (m_delayEcho) {
            m_pendingEchoes.emplace_back(line, sendingOrigin());
        } else if (m_echo) {
            echo(line, sendingOrigin());
        }
        return {};
    }
    // The echo of a written line (with its origin), then - later, like a device - the answer:
    // the output lines and the prompt.
    void echo(const std::string& line, std::uint64_t origin) {
        deliver(Channel::Shell, Direction::In, line, origin);
        if (const auto answer = m_answers.find(line); answer != m_answers.end()) {
            QTimer::singleShot(0, this, [this, lines = answer->second] {
                for (const std::string& text : lines) {
                    output(text);
                }
                prompt();
            });
        }
    }
    // Echoes held back by setDelayEcho(true), in order.
    void releaseEchoes() {
        while (!m_pendingEchoes.empty()) {
            const auto [line, origin] = m_pendingEchoes.front();
            m_pendingEchoes.pop_front();
            echo(line, origin);
        }
    }
    hwio::Status reconnect() override { return {}; }
    [[nodiscard]] const hwio::device::ConsoleLog& log() const override { return m_log; }
    [[nodiscard]] const hwio::device::ConnectionEvent& connection() const override {
        return m_connection;
    }
    [[nodiscard]] hwio::device::SessionIdentity identity() const override { return {}; }
    [[nodiscard]] hwio::device::Capabilities capabilities() const override { return {}; }
    [[nodiscard]] const hwio::device::DeviceProfile& profile() const override { return *m_profile; }
    [[nodiscard]] std::string_view consoleTargetId() const override { return "app"; }
    [[nodiscard]] std::filesystem::path consoleFile() const override { return {}; }
    [[nodiscard]] hwio::Status
    setFirmwareIdentity(std::optional<hwio::device::FirmwareIdentity> identity,
                        std::uint64_t attach) override {
        identities.emplace_back(std::move(identity), attach);
        return {};
    }
    // Plan 0028: what setFirmwareIdentity() received (a test recorder).
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    std::vector<std::pair<std::optional<hwio::device::FirmwareIdentity>, std::uint64_t>> identities;

    void deliver(Channel channel, Direction dir, const std::string& text,
                 std::uint64_t origin = 0) {
        ConsoleLine line;
        line.channel = channel;
        line.dir = dir;
        line.text = text;
        line.origin = origin;
        line.time = std::chrono::system_clock::now();
        if (captureLine(line)) {
            return;
        }
        line.seq = m_log.append(line);
        emit linesReceived(QList<ConsoleLine>{line});
    }
    void output(const std::string& text) { deliver(Channel::Shell, Direction::Out, text); }
    void prompt() { emit promptSeen(); }
    void setState(hwio::device::ConnectionState state) {
        m_connection.state = state;
        emit connectionChanged(m_connection);
    }
    // Plan 0028: a new attach (ConnectionEvent::attach) reported as Connected.
    void attachAgain(std::uint64_t attach) {
        m_connection.attach = attach;
        setState(hwio::device::ConnectionState::Connected);
    }
    void setAttach(std::uint64_t attach) { m_connection.attach = attach; }

    [[nodiscard]] const std::vector<std::string>& written() const noexcept { return m_written; }
    void clearWritten() { m_written.clear(); }
    // The device's answer to `command` (then the prompt).
    void answer(const std::string& command, std::vector<std::string> lines) {
        m_answers[command] = std::move(lines);
    }
    void setEcho(bool echo) { m_echo = echo; }
    void setDelayEcho(bool delay) { m_delayEcho = delay; }

protected:
    void closeSession() override {}

private:
    const hwio::device::DeviceProfile* m_profile;
    hwio::device::ConsoleLog m_log{1000};
    hwio::device::ConnectionEvent m_connection;
    std::vector<std::string> m_written;
    std::map<std::string, std::vector<std::string>> m_answers;
    bool m_echo = true;
    bool m_delayEcho = false;
    std::deque<std::pair<std::string, std::uint64_t>> m_pendingEchoes;
};

bool runUntil(const std::function<bool()>& condition,
              std::chrono::milliseconds limit = std::chrono::seconds(5)) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < limit.count()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return condition();
}

void runFor(std::chrono::milliseconds duration) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < duration.count()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

std::vector<std::string> texts(const std::vector<ConsoleLine>& lines) {
    std::vector<std::string> result;
    result.reserve(lines.size());
    for (const ConsoleLine& line : lines) {
        result.push_back(line.text);
    }
    return result;
}

hwio::device::CaptureResult resultOf(const CommandCapture& capture) {
    return capture.result().value_or(hwio::device::CaptureResult{});
}

ShellQueryResult resultOf(const ShellQuery& query) {
    return query.result().value_or(ShellQueryResult{});
}

CaptureOptions options(bool silent = false) {
    return CaptureOptions{.timeout = std::chrono::milliseconds(2000),
                          .silence = std::chrono::milliseconds(100),
                          .maxLines = 2000,
                          .maxBytes = std::size_t{256} * 1024,
                          .silent = silent};
}

TEST(CommandCaptureTest, EndsAtThePrompt) {
    FakeSession session(promptProfile());
    session.output("stale output of an earlier command");
    auto capture = CommandCapture::start(session, "version", options());
    ASSERT_TRUE(capture);
    session.output("version"); // a shell that echoes the typed line itself
    session.output("Firmware version: v3.1.0");
    session.prompt();
    ASSERT_TRUE((*capture)->isFinished());
    const auto result = resultOf(**capture);
    EXPECT_EQ(result.end, CaptureEnd::Prompt);
    EXPECT_EQ(texts(result.lines), std::vector<std::string>{"Firmware version: v3.1.0"});
    EXPECT_FALSE(result.truncated);
    EXPECT_FALSE(result.interleaved);
    EXPECT_EQ(hwio::device::captureEndId(result.end), "prompt");
}

TEST(CommandCaptureTest, EndsAfterSilenceAndOutputBeforeTheEchoIsNotOurs) {
    FakeSession session; // CHESTER: no RTT prompt
    session.setEcho(false);
    auto capture = CommandCapture::start(session, "lte state", options());
    ASSERT_TRUE(capture);
    session.output("earlier"); // before the echo
    session.deliver(Channel::Shell, Direction::In, "lte state");
    session.output("state: registered");
    session.deliver(Channel::Log, Direction::Out, "<inf> a log line is not shell output");
    session.deliver(Channel::Shell, Direction::In, "other"); // typed by someone else
    ASSERT_TRUE(runUntil([&] { return (*capture)->isFinished(); }));
    const auto result = resultOf(**capture);
    EXPECT_EQ(result.end, CaptureEnd::Silence);
    EXPECT_EQ(texts(result.lines), (std::vector<std::string>{"state: registered", "other"}));
    EXPECT_TRUE(result.interleaved);
}

TEST(CommandCaptureTest, TimeoutWithoutOutput) {
    FakeSession session;
    auto opts = options();
    opts.timeout = std::chrono::milliseconds(150);
    auto capture = CommandCapture::start(session, "reboot", opts);
    ASSERT_TRUE(capture);
    ASSERT_TRUE(runUntil([&] { return (*capture)->isFinished(); }));
    EXPECT_EQ(resultOf(**capture).end, CaptureEnd::Timeout);
    EXPECT_TRUE(resultOf(**capture).lines.empty());
}

TEST(CommandCaptureTest, OutputIsBounded) {
    FakeSession session(promptProfile());
    auto opts = options();
    opts.maxLines = 3;
    auto capture = CommandCapture::start(session, "help", opts);
    ASSERT_TRUE(capture);
    for (int i = 0; i < 10; ++i) {
        session.output("line " + std::to_string(i));
    }
    session.prompt();
    ASSERT_TRUE((*capture)->isFinished());
    EXPECT_EQ(resultOf(**capture).lines.size(), 3U);
    EXPECT_TRUE(resultOf(**capture).truncated);
    // Bytes count too.
    auto bytes = options();
    bytes.maxBytes = 10;
    auto second = CommandCapture::start(session, "help", bytes);
    ASSERT_TRUE(second);
    session.output("12345678");
    session.output("too long for the rest");
    session.prompt();
    EXPECT_EQ(texts(resultOf(**second).lines), std::vector<std::string>{"12345678"});
    EXPECT_TRUE(resultOf(**second).truncated);
}

TEST(CommandCaptureTest, CancelGivesNoResult) {
    FakeSession session(promptProfile());
    auto capture = CommandCapture::start(session, "help", options());
    ASSERT_TRUE(capture);
    (*capture)->cancel();
    session.output("late");
    session.prompt();
    EXPECT_FALSE((*capture)->isFinished());
}

TEST(ShellQueryTest, SilentAndUserLinesWait) {
    FakeSession session(promptProfile());
    session.output("before");
    const auto linesBefore = session.log().lastSeq();
    auto query = ShellQuery::start(session, "info show");
    ASSERT_TRUE(query);
    EXPECT_TRUE(session.shellCaptureActive());
    // A user command meanwhile waits.
    ASSERT_TRUE(session.sendLine("version"));
    EXPECT_EQ(session.written(), std::vector<std::string>{"info show"});
    session.output("fw-name: hio-chester-push-fm");
    session.output("fw-version: v4.0.1");
    session.deliver(Channel::Log, Direction::Out, "<inf> logs still reach the log");
    // A second query is refused while one runs.
    const auto second = ShellQuery::start(session, "help");
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error().code, hwio::ErrorCode::InternalBusy);
    session.prompt();
    ASSERT_TRUE((*query)->isFinished());
    EXPECT_EQ(resultOf(**query).lines,
              (std::vector<std::string>{"fw-name: hio-chester-push-fm", "fw-version: v4.0.1"}));
    // Silent: the echo and output are not in the log (only the log line is); the held command was
    // sent when the query ended.
    EXPECT_EQ(session.log().lastSeq(), linesBefore + 2); // the log line and the user's echo
    EXPECT_EQ(session.written(), (std::vector<std::string>{"info show", "version"}));
    EXPECT_FALSE(session.shellCaptureActive());
}

TEST(ShellQueryTest, RefusedWhenNotConnectedOrBusy) {
    FakeSession session;
    session.setState(hwio::device::ConnectionState::Connecting);
    const auto notConnected = ShellQuery::start(session, "help");
    ASSERT_FALSE(notConnected);
    EXPECT_EQ(notConnected.error().code, hwio::ErrorCode::TargetNotConnected);
    session.setState(hwio::device::ConnectionState::Connected);
    const auto busy = ShellQuery::start(
        session, "help",
        hwio::device::ShellQueryOptions{.timeout = std::chrono::milliseconds(2000),
                                        .maxLines = 2000,
                                        .busy = [] { return true; }});
    ASSERT_FALSE(busy);
    EXPECT_EQ(busy.error().code, hwio::ErrorCode::InternalBusy);
    EXPECT_TRUE(session.written().empty());
}

TEST(ShellQueryTest, TimeoutAndCancelReleaseTheHold) {
    FakeSession session;
    auto query = ShellQuery::start(
        session, "help",
        hwio::device::ShellQueryOptions{
            .timeout = std::chrono::milliseconds(100), .maxLines = 2000, .busy = {}});
    ASSERT_TRUE(query);
    ASSERT_TRUE(session.sendLine("held"));
    const auto result = hwio::device::waitForQuery(**query);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value_or(ShellQueryResult{}).end, CaptureEnd::Timeout);
    EXPECT_EQ(session.written(), (std::vector<std::string>{"help", "held"}));

    // The released line made the shell busy: the next query waits until it is quiet.
    EXPECT_GT(session.shellBusyFor(), std::chrono::milliseconds(0));
    ASSERT_TRUE(runUntil([&] { return session.shellBusyFor() == std::chrono::milliseconds(0); }));
    auto cancelled = ShellQuery::start(session, "help");
    ASSERT_TRUE(cancelled);
    ASSERT_TRUE(session.sendLine("after cancel"));
    (*cancelled)->cancel();
    EXPECT_FALSE(session.shellCaptureActive());
    EXPECT_EQ(session.written().back(), "after cancel");
}

TEST(ShellQueryQueueTest, OneAtATimeAndRefusedOnesAnswerNullopt) {
    FakeSession session(promptProfile());
    session.answer("info show", {"fw-name: hio-chester-push-fm"});
    session.answer("help", {"Available commands:", "  info  :Info commands"});
    ShellQueryQueue queue;
    std::vector<std::string> order;
    std::optional<ShellQueryResult> info;
    std::optional<ShellQueryResult> help;
    bool noSessionAnswered = false;
    queue.run("info show",
              [&](const std::optional<ShellQueryResult>& result) { noSessionAnswered = !result; });
    EXPECT_TRUE(noSessionAnswered); // no session yet
    queue.setSession(&session);
    queue.run("info show", [&](const std::optional<ShellQueryResult>& result) {
        order.emplace_back("info show");
        info = result;
    });
    queue.run("help", [&](const std::optional<ShellQueryResult>& result) {
        order.emplace_back("help");
        help = result;
    });
    EXPECT_TRUE(queue.running());
    EXPECT_EQ(session.written(), std::vector<std::string>{"info show"}); // one at a time
    ASSERT_TRUE(runUntil([&] { return order.size() == 2; }));
    EXPECT_EQ(order, (std::vector<std::string>{"info show", "help"}));
    ASSERT_TRUE(info.has_value() && help.has_value());
    EXPECT_EQ(info.value_or(ShellQueryResult{}).lines,
              std::vector<std::string>{"fw-name: hio-chester-push-fm"});
    EXPECT_EQ(help.value_or(ShellQueryResult{}).end, hwio::device::CaptureEnd::Prompt);
    EXPECT_EQ(session.log().lastSeq(), 0U); // silent
    ASSERT_TRUE(runUntil([&] { return !queue.running(); }));

    // Bounded: beyond maxQueued the query is answered at once.
    int refused = 0;
    for (std::size_t i = 0; i < ShellQueryQueue::maxQueued + 2; ++i) {
        queue.run("unanswered", [&](const std::optional<ShellQueryResult>& result) {
            refused += result ? 0 : 1;
        });
    }
    EXPECT_EQ(refused, 1); // 1 runs, maxQueued wait, 1 refused
    // setSession(nullptr) answers the running one and the waiting ones.
    queue.setSession(nullptr);
    EXPECT_EQ(refused, static_cast<int>(ShellQueryQueue::maxQueued) + 2);
    EXPECT_FALSE(queue.running());
    EXPECT_FALSE(session.shellCaptureActive());
}

TEST(ShellQueryQueueTest, BusyRefusesAndADeletedSessionEndsTheQuery) {
    bool busy = true;
    auto session = std::make_unique<FakeSession>(promptProfile());
    ShellQueryQueue queue([&] { return busy; });
    queue.setSession(session.get());
    bool answered = false;
    std::optional<ShellQueryResult> result{ShellQueryResult{}};
    queue.run("help", [&](const std::optional<ShellQueryResult>& r) {
        answered = true;
        result = r;
    });
    EXPECT_TRUE(answered);
    EXPECT_FALSE(result);
    EXPECT_TRUE(session->written().empty());
    busy = false;
    answered = false;
    queue.run("help", [&](const std::optional<ShellQueryResult>& r) {
        answered = true;
        result = r;
    });
    EXPECT_TRUE(queue.running());
    session.reset();
    ASSERT_TRUE(runUntil([&] { return answered; }));
    EXPECT_FALSE(result);
    ASSERT_TRUE(runUntil([&] { return !queue.running(); }));
}

// The marker line of Zephyr's `help` with CONFIG_SHELL_HELP (ADR-0178).
constexpr const char* dashHMarkerLine =
    "You can try to call commands with <-h> or <--help> parameter for more information.";

// The learner with its queue, cache (in a temporary directory) and a fake Zephyr shell (with
// CONFIG_SHELL_HELP: `help` shows the -h marker).
struct LearnerRig {
    LearnerRig() {
        session.answer("help",
                       {dashHMarkerLine, "", "Available commands:", "  app   :Application commands",
                        "  lte   :LTE commands", "  info  :Info commands"});
        session.answer("lte -h", {"lte - LTE commands", "Subcommands:", "  state  :Get LTE state",
                                  "  test   :Test mode"});
        session.answer("info -h", {"info - Info commands"}); // no subcommands
        queue.setSession(&session);
    }

    [[nodiscard]] std::vector<std::string> names(const std::vector<std::string>& path) const {
        std::vector<std::string> result;
        const auto tree = learner.tree();
        const hwio::device::CommandNode* node = tree ? tree->find(path) : nullptr;
        if (node == nullptr) {
            return result;
        }
        for (const auto& child : node->children) {
            result.push_back(child.name);
        }
        return result;
    }
    [[nodiscard]] std::size_t sent(const std::string& command) const {
        return static_cast<std::size_t>(std::ranges::count(session.written(), command));
    }

    QTemporaryDir dir;
    hwio::device::CommandTreeCache cache{std::filesystem::path(dir.path().toStdU16String())};
    FakeSession session{promptProfile()};
    ShellQueryQueue queue;
    hwio::device::CommandTreeLearner learner{queue, &cache};
};

hwio::device::FirmwareKey learnerKey() {
    return {.profileId = "chester",
            .targetId = "app",
            .fwName = "hio-chester-push-fm",
            .fwVersion = "v4.0.1"};
}

TEST(CommandTreeLearnerTest, ReadsOnConnectThenChildrenOnceAndCaches) {
    LearnerRig rig;
    int readingChanges = 0;
    QObject::connect(&rig.learner, &hwio::device::CommandTreeLearner::readingChanged,
                     [&] { ++readingChanges; });
    rig.learner.start(learnerKey(), true);
    EXPECT_TRUE(rig.learner.reading());
    ASSERT_TRUE(runUntil([&] { return !rig.learner.reading(); }));
    EXPECT_EQ(readingChanges, 2);
    EXPECT_EQ(rig.names({}), (std::vector<std::string>{"app", "lte", "info"})); // device order
    rig.learner.requestChildren({"lte"});
    rig.learner.requestChildren({"lte"}); // once
    rig.learner.requestChildren({"info"});
    rig.learner.requestChildren({"unknown"}); // not a known command: nothing sent
    ASSERT_TRUE(runUntil([&] { return !rig.learner.reading(); }));
    EXPECT_EQ(rig.names({"lte"}), (std::vector<std::string>{"state", "test"}));
    const auto tree = rig.learner.tree();
    ASSERT_TRUE(tree);
    const hwio::device::CommandNode* info = tree->find(std::vector<std::string>{"info"});
    ASSERT_NE(info, nullptr);
    EXPECT_TRUE(info->childrenKnown);
    rig.learner.requestChildren({"info"}); // known to have none
    EXPECT_EQ(rig.sent("lte -h"), 1U);
    EXPECT_EQ(rig.sent("info -h"), 1U);
    EXPECT_EQ(rig.sent("unknown -h"), 0U);
    // Stored per firmware: a second attach of the same firmware asks nothing.
    const hwio::device::CommandTree cached =
        rig.cache.load(learnerKey()).value_or(hwio::device::CommandTree{});
    const hwio::device::CommandNode* lte = cached.find(std::vector<std::string>{"lte"});
    ASSERT_NE(lte, nullptr);
    EXPECT_EQ(lte->children.size(), 2U);
    rig.session.clearWritten();
    rig.learner.start(learnerKey(), true);
    EXPECT_FALSE(rig.learner.reading());
    EXPECT_EQ(rig.names({"lte"}), (std::vector<std::string>{"state", "test"}));
    EXPECT_TRUE(rig.session.written().empty());
}

TEST(CommandTreeLearnerTest, LazyWithoutReadOnConnectAndResetForgets) {
    LearnerRig rig;
    rig.learner.start(learnerKey(), false);
    EXPECT_FALSE(rig.learner.reading());
    EXPECT_FALSE(rig.learner.tree());
    EXPECT_TRUE(rig.session.written().empty());
    rig.learner.requestChildren({"lte"}); // no tree yet: the top level first
    ASSERT_TRUE(runUntil([&] { return !rig.learner.reading() && rig.learner.tree(); }));
    EXPECT_EQ(rig.sent("help"), 1U);
    EXPECT_EQ(rig.sent("lte -h"), 0U);
    rig.learner.requestTopLevel(); // once per attach
    EXPECT_EQ(rig.sent("help"), 1U);

    // A reset while a query runs: its answer is ignored.
    rig.learner.requestChildren({"lte"});
    EXPECT_TRUE(rig.learner.reading());
    rig.learner.reset();
    EXPECT_FALSE(rig.learner.reading());
    EXPECT_FALSE(rig.learner.tree());
    ASSERT_TRUE(runUntil([&] { return !rig.queue.running(); }));
    EXPECT_FALSE(rig.learner.tree());
}

// ADR-0178: without the CONFIG_SHELL_HELP marker in `help` no `-h` is ever sent; the top level
// (bare names) is learned and cached with the flag, so a cached firmware is not asked either.
TEST(CommandTreeLearnerTest, NoDashHWithoutTheMarker) {
    LearnerRig rig;
    rig.session.answer("help", {"Available commands:", "  app", "  lte", "  info"});
    rig.learner.start(learnerKey(), true);
    ASSERT_TRUE(runUntil([&] { return !rig.learner.reading() && rig.learner.tree(); }));
    EXPECT_EQ(rig.names({}), (std::vector<std::string>{"app", "lte", "info"}));
    EXPECT_FALSE(rig.learner.tree()->dashHSupported());
    rig.learner.requestChildren({"lte"});
    rig.learner.requestChildren({"info"});
    EXPECT_FALSE(rig.learner.reading());
    runFor(std::chrono::milliseconds(50));
    EXPECT_EQ(rig.sent("lte -h"), 0U);
    EXPECT_EQ(rig.sent("info -h"), 0U);
    EXPECT_EQ(rig.session.written(), (std::vector<std::string>{"help"}));
    const auto cached = rig.cache.load(learnerKey());
    ASSERT_TRUE(cached.has_value());
    EXPECT_FALSE(cached.transform(&hwio::device::CommandTree::dashHSupported).value_or(true));

    // The next attach of the same firmware: the cached flag, neither `help` nor `-h`.
    rig.session.clearWritten();
    rig.learner.start(learnerKey(), true);
    ASSERT_TRUE(rig.learner.tree());
    rig.learner.requestChildren({"lte"});
    runFor(std::chrono::milliseconds(50));
    EXPECT_TRUE(rig.session.written().empty());
}

// ADR-0178: with the marker `-h` is sent; the flag is cached, so a cached firmware asks `-h`
// without `help`.
TEST(CommandTreeLearnerTest, DashHWithTheMarkerAndFromTheCache) {
    LearnerRig rig;
    rig.learner.start(learnerKey(), true);
    ASSERT_TRUE(runUntil([&] { return !rig.learner.reading() && rig.learner.tree(); }));
    EXPECT_TRUE(rig.learner.tree()->dashHSupported());
    EXPECT_TRUE(rig.cache.load(learnerKey())
                    .transform(&hwio::device::CommandTree::dashHSupported)
                    .value_or(false));

    rig.session.clearWritten();
    rig.learner.start(learnerKey(), true); // from the cache
    ASSERT_TRUE(rig.learner.tree());
    EXPECT_TRUE(rig.learner.tree()->dashHSupported());
    rig.learner.requestChildren({"lte"});
    ASSERT_TRUE(runUntil([&] { return !rig.learner.reading(); }));
    EXPECT_EQ(rig.names({"lte"}), (std::vector<std::string>{"state", "test"}));
    EXPECT_EQ(rig.session.written(), (std::vector<std::string>{"lte -h"}));
}

TEST(CommandTreeLearnerTest, NoCommandListLeavesNoTree) {
    LearnerRig rig;
    rig.session.answer("help", {"help: command not found"});
    rig.learner.start(std::nullopt, true);
    ASSERT_TRUE(runUntil([&] { return !rig.learner.reading(); }));
    EXPECT_FALSE(rig.learner.tree());
}

// ---- review P1-P5 -------------------------------------------------------------------------------

std::vector<std::string> logTexts(const FakeSession& session) {
    std::vector<std::string> result;
    for (const ConsoleLine& line : session.log().linesSince(0, hwio::device::ChannelMask::Shell)) {
        result.push_back(line.text);
    }
    return result;
}

bool idle(const FakeSession& session) {
    return session.shellBusyFor() == std::chrono::milliseconds(0);
}

// P1 (b, c): a silent capture takes only its own echo (by origin, not text) and never the echo of
// another line after it; a same-text line of the user stays in the log.
TEST(CommandCaptureTest, SilentKnowsItsEchoByOrigin) {
    FakeSession session(promptProfile());
    session.setDelayEcho(true);
    auto capture = CommandCapture::start(session, "help", options(true));
    ASSERT_TRUE(capture);
    session.deliver(Channel::Shell, Direction::In, "help"); // the user's own `help`, in flight
    session.output("user help output");
    session.releaseEchoes(); // now our echo
    session.output("ours");
    session.deliver(Channel::Shell, Direction::In, "other"); // another line after our echo
    session.prompt();
    ASSERT_TRUE((*capture)->isFinished());
    EXPECT_EQ(texts(resultOf(**capture).lines), std::vector<std::string>{"ours"});
    EXPECT_FALSE(resultOf(**capture).interleaved);
    EXPECT_EQ(logTexts(session), (std::vector<std::string>{"help", "user help output", "other"}));
}

// P1 (a): with a prompt, idle means no line without an echo and no echo without a prompt after it.
TEST(ShellQueryTest, StartsOnlyOnAnIdleShellWithAPrompt) {
    FakeSession session(promptProfile());
    EXPECT_TRUE(idle(session));
    session.setDelayEcho(true);
    ASSERT_TRUE(session.sendLine("version")); // in flight: no echo yet
    auto refused = ShellQuery::start(session, "info show");
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, hwio::ErrorCode::InternalBusy);
    session.releaseEchoes();
    session.setDelayEcho(false);
    EXPECT_FALSE(idle(session)); // echoed, no prompt yet
    session.output("Firmware version: v3.1.0");
    EXPECT_FALSE(idle(session));
    session.prompt();
    EXPECT_TRUE(idle(session));
    EXPECT_TRUE(ShellQuery::start(session, "info show"));
}

// P1 (a): without a prompt, idle means shellQuietTime without shell lines or sent lines.
TEST(ShellQueryTest, StartsOnlyOnAQuietShellWithoutAPrompt) {
    FakeSession session; // CHESTER: no RTT prompt
    session.output("output of an earlier command");
    const auto busy = session.shellBusyFor();
    EXPECT_GT(busy, std::chrono::milliseconds(0));
    EXPECT_LE(busy, hwio::device::ConsoleSession::shellQuietTime);
    EXPECT_FALSE(ShellQuery::start(session, "info show"));
    ASSERT_TRUE(runUntil([&] { return idle(session); }));
    EXPECT_TRUE(ShellQuery::start(session, "info show"));
}

// P1 (a): the queue waits for the idle shell: output of an earlier command that arrives after the
// next line's echo is not taken; a same-text line in flight keeps its echo and answer.
TEST(ShellQueryQueueTest, WaitsForAnIdleShell) {
    FakeSession session(promptProfile());
    session.answer("help", {"Available commands:", "  info  :Info commands"});
    ShellQueryQueue queue;
    queue.setSession(&session);
    ASSERT_TRUE(session.sendLine("lte state")); // echoed, still answering
    std::optional<ShellQueryResult> result;
    bool answered = false;
    queue.run("help", [&](const std::optional<ShellQueryResult>& r) {
        result = r;
        answered = true;
    });
    EXPECT_FALSE(queue.running());
    ASSERT_TRUE(session.sendLine("version"));
    session.output("lte: registered"); // `lte state` answers after the next line's echo
    runFor(std::chrono::milliseconds(150));
    EXPECT_FALSE(queue.running());
    EXPECT_EQ(session.written(), (std::vector<std::string>{"lte state", "version"}));
    session.prompt();
    // A same-text `help` of the user in flight: its echo and answer stay in the log.
    session.setDelayEcho(true);
    ASSERT_TRUE(session.sendLine("help"));
    runFor(std::chrono::milliseconds(150));
    EXPECT_FALSE(queue.running()); // the user's line has no echo yet
    session.setDelayEcho(false);
    session.releaseEchoes();
    ASSERT_TRUE(runUntil([&] { return answered; }));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value_or(ShellQueryResult{}).lines,
              (std::vector<std::string>{"Available commands:", "  info  :Info commands"}));
    EXPECT_EQ(logTexts(session),
              (std::vector<std::string>{"lte state", "version", "lte: registered", "help",
                                        "Available commands:", "  info  :Info commands"}));
}

// P3: a disconnect ends a running query at once without a result.
TEST(ShellQueryQueueTest, ADisconnectEndsTheQueryWithoutAResult) {
    FakeSession session(promptProfile());
    ShellQueryQueue queue;
    queue.setSession(&session);
    bool answered = false;
    std::optional<ShellQueryResult> result{ShellQueryResult{}};
    queue.run("help", [&](const std::optional<ShellQueryResult>& r) {
        answered = true;
        result = r;
    });
    ASSERT_TRUE(queue.running());
    session.setState(hwio::device::ConnectionState::Disconnected);
    EXPECT_TRUE(answered);
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(session.shellCaptureActive());
}

// P4: answers of setSession(nullptr) cannot start a query on the released session.
TEST(ShellQueryQueueTest, ARunFromAClearedCallbackIsAnsweredWithNullopt) {
    FakeSession session(promptProfile());
    ShellQueryQueue queue;
    queue.setSession(&session);
    bool second = false;
    queue.run("info show", [&](const std::optional<ShellQueryResult>& r) {
        EXPECT_FALSE(r.has_value());
        queue.run("help", [&](const std::optional<ShellQueryResult>& again) {
            second = !again.has_value();
        });
    });
    queue.setSession(nullptr);
    EXPECT_TRUE(second);
    EXPECT_EQ(session.written(), std::vector<std::string>{"info show"});
    EXPECT_FALSE(queue.running());
}

// P5: a session deleted while the next query runs (the finished one not deleted yet) ends only
// that query, once.
TEST(ShellQueryQueueTest, ASessionGoneAfterAFinishedQueryEndsTheNextOnce) {
    auto session = std::make_unique<FakeSession>(promptProfile());
    session->answer("info show", {"fw-name: x"});
    ShellQueryQueue queue;
    queue.setSession(session.get());
    bool first = false;
    int secondAnswers = 0;
    queue.run("info show", [&](const std::optional<ShellQueryResult>&) { first = true; });
    queue.run("help", [&](const std::optional<ShellQueryResult>& r) {
        ++secondAnswers;
        EXPECT_FALSE(r.has_value());
    });
    ASSERT_TRUE(runUntil([&] { return first && queue.running(); }));
    session.reset();
    ASSERT_TRUE(runUntil([&] { return !queue.running(); }));
    runFor(std::chrono::milliseconds(50));
    EXPECT_EQ(secondAnswers, 1);
}

// P2: a truncated `help` is neither applied nor cached and not asked again before retryInterval;
// a query that did not run (disconnected) can be asked again at once (P3).
TEST(CommandTreeLearnerTest, IncompleteAnswersAreNotLearned) {
    LearnerRig rig;
    std::vector<std::string> many{"Available commands:"};
    for (std::size_t i = 0; i <= hwio::device::ShellQuery::defaultMaxLines; ++i) {
        many.push_back("  c" + std::to_string(i) + "  :command");
    }
    rig.session.answer("help", many);
    rig.learner.start(learnerKey(), true);
    ASSERT_TRUE(runUntil([&] { return !rig.learner.reading(); }));
    EXPECT_FALSE(rig.learner.tree());
    EXPECT_FALSE(rig.cache.load(learnerKey()).has_value());
    ASSERT_TRUE(runUntil([&] { return idle(rig.session); }));
    rig.learner.requestTopLevel(); // backing off
    EXPECT_EQ(rig.sent("help"), 1U);

    LearnerRig again;
    again.session.setEcho(false); // the query waits for its echo
    again.learner.start(std::nullopt, true);
    ASSERT_TRUE(runUntil([&] { return again.queue.running(); }));
    again.session.setState(hwio::device::ConnectionState::Disconnected);
    EXPECT_FALSE(again.learner.reading());
    again.session.setState(hwio::device::ConnectionState::Connected);
    again.session.setEcho(true);
    again.learner.requestTopLevel();
    ASSERT_TRUE(runUntil([&] { return !again.learner.reading(); }));
    EXPECT_EQ(again.sent("help"), 2U);
    EXPECT_TRUE(again.learner.tree());
}

// P2: a `-h` that times out is not stored as "no subcommands" (nothing applied or cached); it is
// asked again only after retryInterval.
TEST(CommandTreeLearnerTest, ATimedOutQueryIsNotLearned) {
    LearnerRig rig;
    rig.learner.start(learnerKey(), true);
    ASSERT_TRUE(runUntil([&] { return !rig.learner.reading(); }));
    ASSERT_TRUE(rig.learner.tree());
    const std::vector<std::string> app{"app"}; // no answer: the query times out (2 s)
    rig.learner.requestChildren(app);
    ASSERT_TRUE(rig.learner.reading());
    ASSERT_TRUE(runUntil([&] { return !rig.learner.reading(); }, std::chrono::seconds(10)));
    const hwio::device::CommandNode* node = rig.learner.tree()->find(app);
    ASSERT_NE(node, nullptr);
    EXPECT_FALSE(node->childrenKnown);
    const auto cached = rig.cache.load(learnerKey());
    ASSERT_TRUE(cached.has_value());
    const hwio::device::CommandNode* cachedNode = cached ? cached->find(app) : nullptr;
    ASSERT_NE(cachedNode, nullptr);
    EXPECT_FALSE(cachedNode->childrenKnown);
    rig.learner.requestChildren(app); // backing off
    EXPECT_EQ(rig.sent("app -h"), 1U);
}

// User decision 2026-10-26: with a prompt, a command that never prints it again counts as busy
// only until promptWaitExpiry of shell silence; then a query is accepted.
TEST(ShellQueryTest, AMissingPromptExpiresAfterSilence) {
    FakeSession session(promptProfile());
    ASSERT_TRUE(session.sendLine("app config show")); // echoed, never a prompt
    session.output("some output");
    EXPECT_FALSE(idle(session));
    EXPECT_FALSE(ShellQuery::start(session, "info show"));
    runFor(hwio::device::ConsoleSession::promptWaitExpiry / 2);
    session.output("more output"); // output restarts the silence
    runFor(hwio::device::ConsoleSession::promptWaitExpiry / 2 + std::chrono::milliseconds(200));
    EXPECT_FALSE(idle(session));
    ASSERT_TRUE(runUntil([&] { return idle(session); }, std::chrono::seconds(4)));
    EXPECT_TRUE(ShellQuery::start(session, "info show"));
}

// ---- DeviceInfoWatcher (plan 0028 D7) ------------------------------------------------------

const std::vector<std::string>& realInfoShow() {
    static const std::vector<std::string> lines{"vendor name: HARDWARIO a.s.",
                                                "product name: CHESTER-M",
                                                "hardware variant: CDGLS",
                                                "hardware revision: R3.2",
                                                "firmware bundle: (unset)",
                                                "firmware name: CHESTER Clime",
                                                "firmware version: v3.0.0",
                                                "serial number: 2159018247",
                                                "claim token: c1a1m70ken5ecre7",
                                                "ble devaddr: 00:11:22:33:44:55",
                                                "ble passkey: 654321"};
    return lines;
}

const hwio::device::FirmwareIdentity clime{
    .name = "CHESTER Clime", .version = "v3.0.0", .deviceSerial = "2159018247"};

QString capturedLog;              // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
QtMessageHandler previousHandler; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void captureInfoAndAbove(QtMsgType type, const QMessageLogContext& context, const QString& text) {
    if (type != QtDebugMsg) {
        capturedLog += text + QLatin1Char('\n');
    }
    if (previousHandler != nullptr) {
        previousHandler(type, context, text);
    }
}

TEST(DeviceInfoWatcherTest, AnswerOfTheCurrentAttachNamesTheFirmware) {
    FakeSession session;
    session.setAttach(1);
    session.answer("info show", realInfoShow());
    ShellQueryQueue queries;
    queries.setSession(&session);
    capturedLog.clear();
    previousHandler = qInstallMessageHandler(captureInfoAndAbove);
    hwio::device::DeviceInfoWatcher watcher(session, queries);
    int answered = 0;
    QObject::connect(&watcher, &hwio::device::DeviceInfoWatcher::answered, [&] { ++answered; });
    const bool done = runUntil([&] { return answered > 0; });
    qInstallMessageHandler(previousHandler);
    ASSERT_TRUE(done);
    EXPECT_EQ(watcher.info().fwName(), "CHESTER Clime");
    ASSERT_EQ(session.identities.size(), 1U);
    EXPECT_EQ(session.identities[0].first, clime);
    EXPECT_EQ(session.identities[0].second, 1U);
    // The name and version are logged; the secrets of `info show` never (above debug level).
    EXPECT_TRUE(capturedLog.contains(QStringLiteral("CHESTER Clime")));
    EXPECT_FALSE(capturedLog.contains(QStringLiteral("c1a1m70ken5ecre7")));
    EXPECT_FALSE(capturedLog.contains(QStringLiteral("654321")));
    EXPECT_FALSE(capturedLog.contains(QStringLiteral("2159018247")));
}

TEST(DeviceInfoWatcherTest, AnAnswerWithoutFirmwarePassesNoIdentity) {
    FakeSession session;
    session.setAttach(3);
    session.answer("info show", {"vendor name: HARDWARIO a.s."});
    ShellQueryQueue queries;
    queries.setSession(&session);
    hwio::device::DeviceInfoWatcher watcher(session, queries);
    ASSERT_TRUE(runUntil([&] { return !session.identities.empty(); }));
    EXPECT_FALSE(session.identities[0].first.has_value());
    EXPECT_EQ(session.identities[0].second, 3U);
    EXPECT_FALSE(watcher.info().hasFirmware());
}

TEST(DeviceInfoWatcherTest, AStaleAnswerIsIgnoredAndADisconnectClears) {
    FakeSession session;
    session.setState(hwio::device::ConnectionState::Disconnected);
    session.answer("info show", realInfoShow());
    ShellQueryQueue queries;
    queries.setSession(&session);
    hwio::device::DeviceInfoWatcher watcher(session, queries);
    session.setDelayEcho(true);
    session.attachAgain(1); // the query of attach 1 waits for its echo
    ASSERT_TRUE(runUntil([&] { return session.written().size() == 1U; }));
    session.setState(hwio::device::ConnectionState::Disconnected); // ends it without a result
    session.setDelayEcho(false);
    session.releaseEchoes();
    session.attachAgain(2);
    ASSERT_TRUE(runUntil([&] { return !session.identities.empty(); }));
    runFor(std::chrono::milliseconds(100));
    ASSERT_EQ(session.identities.size(), 1U);
    EXPECT_EQ(session.identities[0].second, 2U);
    int changed = 0;
    QObject::connect(&watcher, &hwio::device::DeviceInfoWatcher::infoChanged, [&] { ++changed; });
    session.setState(hwio::device::ConnectionState::Disconnected);
    EXPECT_EQ(changed, 1);
    EXPECT_TRUE(watcher.info().fields.empty());
}

// Review round 1 (R8): a refused query (the caller is busy) and a query without an answer (it
// times out) both tell the session "no identity" for the attach (D10: the banner may be used).
TEST(DeviceInfoWatcherTest, ARefusedQueryPassesNoIdentity) {
    FakeSession session;
    session.setAttach(4);
    session.answer("info show", realInfoShow());
    ShellQueryQueue queries([] { return true; }); // always busy: refused
    queries.setSession(&session);
    const hwio::device::DeviceInfoWatcher watcher(session, queries);
    ASSERT_TRUE(runUntil([&] { return !session.identities.empty(); },
                         ShellQueryQueue::maxIdleWait + std::chrono::seconds(3)));
    EXPECT_FALSE(session.identities[0].first.has_value());
    EXPECT_EQ(session.identities[0].second, 4U);
    EXPECT_TRUE(session.written().empty()); // nothing was sent
    EXPECT_TRUE(watcher.info().fields.empty());
}

TEST(DeviceInfoWatcherTest, ATimedOutQueryPassesNoIdentity) {
    FakeSession session(promptProfile()); // a prompt that never comes: the query times out
    session.setAttach(5);
    ShellQueryQueue queries;
    queries.setSession(&session);
    const hwio::device::DeviceInfoWatcher watcher(session, queries);
    ASSERT_TRUE(runUntil([&] { return !session.identities.empty(); }, std::chrono::seconds(8)));
    EXPECT_FALSE(session.identities[0].first.has_value());
    EXPECT_EQ(session.identities[0].second, 5U);
    EXPECT_EQ(session.written(), std::vector<std::string>{"info show"});
}

} // namespace
