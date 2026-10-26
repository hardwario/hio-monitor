// Plan 0021 review P6: the CLI console's command tree learning follows the connection - forgotten
// when the console leaves Connected, `info show` and then `help` again after it reconnects; a
// query that did not get an answer is asked again.

#include "console_learning.h"
#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/console_session.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QList>
#include <QTemporaryDir>
#include <QTimer>
#include <QtCore/qtmetamacros.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using hio::device::Channel;
using hio::device::ConnectionState;
using hio::device::ConsoleLine;
using hio::device::Direction;

// CHESTER with an RTT prompt on the app target (the prompt rule ends the queries at once).
const hio::device::DeviceProfile& promptProfile() {
    static const std::array<hio::device::TargetProfile, 2> targets = [] {
        const auto* chester = hio::device::ProfileRegistry::find("chester");
        std::array<hio::device::TargetProfile, 2> copy{chester->targets[0], chester->targets[1]};
        copy[0].shellPrompt = "uart:~$ ";
        return copy;
    }();
    static const hio::device::DeviceProfile profile = [] {
        hio::device::DeviceProfile copy = *hio::device::ProfileRegistry::find("chester");
        copy.targets = targets;
        return copy;
    }();
    return profile;
}

// Echoes every written line (with its origin) and answers the known commands, then the prompt.
class FakeSession final : public hio::device::ConsoleSession {
public:
    FakeSession() { m_connection.state = ConnectionState::Disconnected; }

    hio::Status sendLine(std::string line) override {
        if (hio::Status held; holdLine(line, held)) {
            return held;
        }
        m_written.push_back(line);
        lineQueued();
        deliver(Direction::In, line, sendingOrigin());
        if (const auto answer = m_answers.find(line); answer != m_answers.end()) {
            QTimer::singleShot(0, this, [this, lines = answer->second] {
                for (const std::string& text : lines) {
                    deliver(Direction::Out, text, 0);
                }
                emit promptSeen();
            });
        }
        return {};
    }
    hio::Status reconnect() override { return {}; }
    [[nodiscard]] const hio::device::ConsoleLog& log() const override { return m_log; }
    [[nodiscard]] const hio::device::ConnectionEvent& connection() const override {
        return m_connection;
    }
    [[nodiscard]] hio::device::SessionIdentity identity() const override { return {}; }
    [[nodiscard]] hio::device::Capabilities capabilities() const override { return {}; }
    [[nodiscard]] const hio::device::DeviceProfile& profile() const override {
        return promptProfile();
    }
    [[nodiscard]] std::string_view consoleTargetId() const override { return "app"; }
    [[nodiscard]] std::filesystem::path consoleFile() const override { return {}; }

    void setState(ConnectionState state) {
        m_connection.state = state;
        emit connectionChanged(m_connection);
    }
    void answer(const std::string& command, std::vector<std::string> lines) {
        m_answers[command] = std::move(lines);
    }
    void forget(const std::string& command) { m_answers.erase(command); }
    [[nodiscard]] std::size_t sent(const std::string& command) const {
        return static_cast<std::size_t>(std::ranges::count(m_written, command));
    }

protected:
    void closeSession() override {}

private:
    void deliver(Direction dir, const std::string& text, std::uint64_t origin) {
        ConsoleLine line;
        line.channel = Channel::Shell;
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

    hio::device::ConsoleLog m_log{1000};
    hio::device::ConnectionEvent m_connection;
    std::vector<std::string> m_written;
    std::map<std::string, std::vector<std::string>> m_answers;
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

TEST(ConsoleLearningTest, ARefusedQueryIsAskedAgainAfterAReconnect) {
    const QTemporaryDir dir;
    FakeSession session;
    session.answer("info show", {"fw-name: hio-chester-push-fm", "fw-version: v4.0.1"});
    hio::cli::ConsoleLearning learning(session, "chester", "app", true,
                                       std::filesystem::path(dir.path().toStdU16String()));
    auto& learner = learning.learner();
    EXPECT_EQ(session.sent("info show"), 0U); // not connected yet

    // First attach: `info show` answers, `help` gets no answer before the console disconnects.
    session.setState(ConnectionState::Connected);
    ASSERT_TRUE(runUntil([&] { return session.sent("help") == 1; }));
    EXPECT_TRUE(learner.reading());
    session.setState(ConnectionState::Disconnected);
    EXPECT_FALSE(learner.reading());
    EXPECT_FALSE(learner.tree());
    EXPECT_FALSE(session.shellCaptureActive());

    // Reconnected: asked again from `info show` on, and learned this time.
    session.answer("help", {"Available commands:", "  info  :Info commands"});
    session.setState(ConnectionState::Connected);
    ASSERT_TRUE(runUntil([&] { return learner.tree() != nullptr; }));
    EXPECT_EQ(session.sent("info show"), 2U);
    EXPECT_EQ(session.sent("help"), 2U);
    const std::vector<std::string> info{"info"};
    EXPECT_NE(learner.tree()->find(info), nullptr);

    // Disconnected again: forgotten; the next attach takes the tree from the cache.
    session.setState(ConnectionState::Stopped);
    EXPECT_FALSE(learner.tree());
    session.setState(ConnectionState::Connected);
    ASSERT_TRUE(runUntil([&] { return learner.tree() != nullptr; }));
    EXPECT_EQ(session.sent("info show"), 3U);
    EXPECT_EQ(session.sent("help"), 2U);
}

// A refused `info show` (no answer) starts nothing; the next attach asks again.
TEST(ConsoleLearningTest, NoInfoAnswerStartsNothing) {
    const QTemporaryDir dir;
    FakeSession session;
    session.setState(ConnectionState::Connected);
    session.answer("help", {"Available commands:", "  info  :Info commands"});
    hio::cli::ConsoleLearning learning(session, "chester", "app", true,
                                       std::filesystem::path(dir.path().toStdU16String()));
    ASSERT_TRUE(runUntil([&] { return session.sent("info show") == 1; }));
    session.setState(ConnectionState::Disconnected); // the query ends without a result
    EXPECT_EQ(session.sent("help"), 0U);
    EXPECT_FALSE(learning.learner().tree());
    session.answer("info show", {"fw-name: hio-chester-push-fm", "fw-version: v4.0.1"});
    session.setState(ConnectionState::Connected);
    ASSERT_TRUE(runUntil([&] { return learning.learner().tree() != nullptr; }));
    EXPECT_EQ(session.sent("info show"), 2U);
}

} // namespace
