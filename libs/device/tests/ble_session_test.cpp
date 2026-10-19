// BleSession (plan 0011 B4) on a BleHost whose IBleApi is a GMock driven from the test: the test
// injects listener events on the BLE thread (ADR-0080). Covers open, the data path (line
// assembly, prompt detection, routing, console file), connection states, pairing events,
// reconnect only while seen, explicit reconnect, close during connect, the event queue bound and
// the shared session registry (B2).

#include "ble_session_internal.h"
#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/ble_session.h"
#include "hio/device/console_line.h"
#include "hio/device/console_session.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/device/session_key.h"
#include "hio/device/session_registry.h"
#include "hio/hw/ble/ble_api.h"
#include "hio/hw/ble/ble_host.h"
#include "hio/hw/ble/nus_link.h"
#include "hio/hw/ble/types.h"
#include "mock_ble_api.h"
#include "session_test_support.h"

#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QTemporaryDir>
#include <QtCore/qnamespace.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <qtypes.h>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hio::ErrorCode;
using hio::ble::IBleListener;
using hio::ble::LinkId;
using hio::ble::LinkReady;
using hio::ble::WriteMode;
using hio::device::BleSession;
using hio::device::BleSessionOptions;
using hio::device::Channel;
using hio::device::ConnectionEvent;
using hio::device::ConnectionState;
using hio::device::ConsoleLine;
using hio::device::Direction;
using hio::device::SessionRegistry;
using hio::device::test::spin;
using hio::device::test::waitFor;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using namespace std::chrono_literals;

constexpr const char* deviceId = "F1:22:33:44:55:66";

// What the API saw and how to reach the BLE thread (mutex: the API runs there).
struct Driver {
    std::mutex mutex;
    std::unique_ptr<QObject> context; // lives on the BLE thread
    IBleListener* listener = nullptr;
    int connects = 0;
    int disconnects = 0;
    int scans = 0;
    int stopScans = 0;
    std::vector<std::pair<std::string, std::uint64_t>> writes;
    std::vector<std::string> calls;     // "connect <id>", "disconnect <id>" in order
    std::optional<ErrorCode> scanError; // startScan() fails with it
    int actionsDone = 0;                // run() actions finished on the BLE thread
    hio::Result<LinkId> nextConnect = LinkId{1};

    // Runs `action` with the listener on the BLE thread.
    void run(std::function<void(IBleListener&)> action) {
        QObject* target = nullptr;
        {
            const std::scoped_lock lock(mutex);
            target = context.get();
        }
        QMetaObject::invokeMethod(
            target,
            [this, action = std::move(action)] {
                IBleListener* current = nullptr;
                {
                    const std::scoped_lock lock(mutex);
                    current = listener;
                }
                if (current != nullptr) {
                    action(*current);
                }
                const std::scoped_lock lock(mutex);
                ++actionsDone;
            },
            Qt::QueuedConnection);
    }
    int count(int Driver::* field) {
        const std::scoped_lock lock(mutex);
        return this->*field;
    }
    std::vector<std::pair<std::string, std::uint64_t>> written() {
        const std::scoped_lock lock(mutex);
        return writes;
    }
    std::vector<std::string> history() {
        const std::scoped_lock lock(mutex);
        return calls;
    }
    void setNextConnect(LinkId link) {
        const std::scoped_lock lock(mutex);
        nextConnect = link;
    }
};

class DrivenApi : public NiceMock<hio::ble::test::MockBleApi> {
public:
    explicit DrivenApi(std::shared_ptr<Driver> driver) : m_driver(std::move(driver)) {
        {
            const std::scoped_lock lock(m_driver->mutex);
            m_driver->context = std::make_unique<QObject>();
        }
        ON_CALL(*this, setListener(_)).WillByDefault([this](IBleListener* listener) {
            const std::scoped_lock lock(m_driver->mutex);
            m_driver->listener = listener;
        });
        ON_CALL(*this, adapter()).WillByDefault(Return(hio::ble::test::readyAdapter()));
        ON_CALL(*this, connect(_, _)).WillByDefault([this](const auto&, const auto&) {
            const std::scoped_lock lock(m_driver->mutex);
            ++m_driver->connects;
            if (m_driver->nextConnect) {
                m_driver->calls.push_back("connect " + std::to_string(*m_driver->nextConnect));
            }
            return m_driver->nextConnect;
        });
        ON_CALL(*this, disconnect(_)).WillByDefault([this](LinkId link) {
            const std::scoped_lock lock(m_driver->mutex);
            ++m_driver->disconnects;
            m_driver->calls.push_back("disconnect " + std::to_string(link));
        });
        ON_CALL(*this, startScan(_)).WillByDefault([this](std::chrono::milliseconds) {
            const std::scoped_lock lock(m_driver->mutex);
            ++m_driver->scans;
            if (m_driver->scanError) {
                return hio::Status(std::unexpected(hio::makeError(*m_driver->scanError)));
            }
            return hio::Status{};
        });
        ON_CALL(*this, stopScan()).WillByDefault([this] {
            const std::scoped_lock lock(m_driver->mutex);
            ++m_driver->stopScans;
        });
        ON_CALL(*this, write(_, _, _, _))
            .WillByDefault(
                [this](LinkId, std::span<const std::byte> chunk, WriteMode, std::uint64_t token) {
                    std::string bytes;
                    for (const std::byte b : chunk) {
                        bytes.push_back(static_cast<char>(b));
                    }
                    const std::scoped_lock lock(m_driver->mutex);
                    m_driver->writes.emplace_back(std::move(bytes), token);
                    return hio::Status{};
                });
    }
    DrivenApi(const DrivenApi&) = delete;
    DrivenApi& operator=(const DrivenApi&) = delete;
    DrivenApi(DrivenApi&&) = delete;
    DrivenApi& operator=(DrivenApi&&) = delete;
    ~DrivenApi() override {
        const std::scoped_lock lock(m_driver->mutex);
        m_driver->listener = nullptr;
        m_driver->context.reset();
    }

private:
    std::shared_ptr<Driver> m_driver;
};

struct Events {
    std::vector<ConnectionEvent> connections;
    std::vector<ConsoleLine> lines;
    std::vector<hio::Error> warnings;
    int prompts = 0;
    int pairingPrompts = 0;
    std::vector<hio::Status> pairings;
    std::uint64_t dropped = 0;

    [[nodiscard]] bool reached(ConnectionState state) const {
        return std::ranges::any_of(connections,
                                   [state](const auto& event) { return event.state == state; });
    }
    [[nodiscard]] std::vector<std::string> texts(Channel channel, Direction dir) const {
        std::vector<std::string> result;
        for (const ConsoleLine& line : lines) {
            if (line.channel == channel && line.dir == dir) {
                result.push_back(line.text);
            }
        }
        return result;
    }
};

class BleSessionTest : public ::testing::Test {
public:
    BleSessionTest()
        : m_registry(std::make_unique<SessionRegistry>(
              hio::device::RegistryOptions{.instanceRoot = root() / "instances",
                                           .lockDirectory = root() / "locks",
                                           .consoleDirectory = root() / "console"})),
          m_host(std::make_unique<hio::ble::BleHost>(
              [driver = m_driver]() -> std::unique_ptr<hio::ble::IBleApi> {
                  return std::make_unique<DrivenApi>(driver);
              })) {}

    ~BleSessionTest() override {
        m_sessions.clear();
        m_host->stop();
    }
    BleSessionTest(const BleSessionTest&) = delete;
    BleSessionTest& operator=(const BleSessionTest&) = delete;
    BleSessionTest(BleSessionTest&&) = delete;
    BleSessionTest& operator=(BleSessionTest&&) = delete;

protected:
    [[nodiscard]] fs::path root() const { return {m_dir.path().toStdString()}; }
    [[nodiscard]] Driver* driver() const { return m_driver.get(); }
    [[nodiscard]] SessionRegistry* registry() const { return m_registry.get(); }
    // Closes and drops the session opened last.
    void dropLastSession() { m_sessions.pop_back(); }
    // Stops the BLE host: every link and console file is closed when it returns.
    void stopHost() { m_host->stop(); }

    static BleSessionOptions options(bool autoReconnect = false, std::string id = deviceId) {
        BleSessionOptions result;
        result.profile = hio::device::ProfileRegistry::find("chester");
        result.deviceId = std::move(id);
        result.deviceName = "CHESTER-ABC123";
        result.autoReconnect = autoReconnect;
        result.consoleFile = fs::path(); // none unless a test wants one
        return result;
    }

    static hio::device::detail::BleSessionTiming timing() {
        hio::device::detail::BleSessionTiming result;
        result.link.pairingPromptDelay = 50ms;
        result.link.writeTimeout = 2s;
        result.reconnectInterval = 100ms;
        result.seenWindow = 15s;
        return result;
    }

    hio::Result<std::unique_ptr<BleSession>> open(BleSessionOptions opts) {
        return hio::device::detail::openBleSession(*m_registry, *m_host, std::move(opts), timing());
    }

    BleSession* openRecorded(BleSessionOptions opts, Events*& events) {
        auto session = open(std::move(opts));
        EXPECT_TRUE(session) << session.error().message;
        if (!session) {
            return nullptr;
        }
        m_recorded.push_back(std::make_unique<Events>());
        events = m_recorded.back().get();
        Events* sink = events;
        BleSession* raw = session->get();
        QObject::connect(
            raw, &BleSession::connectionChanged, raw,
            [sink](const ConnectionEvent& event) { sink->connections.push_back(event); });
        QObject::connect(raw, &BleSession::linesReceived, raw,
                         [sink](const QList<ConsoleLine>& lines) {
                             for (const ConsoleLine& line : lines) {
                                 sink->lines.push_back(line);
                             }
                         });
        QObject::connect(raw, &BleSession::warning, raw,
                         [sink](const hio::Error& error) { sink->warnings.push_back(error); });
        QObject::connect(raw, &BleSession::promptSeen, raw, [sink] { ++sink->prompts; });
        QObject::connect(raw, &BleSession::pairingPrompt, raw, [sink] { ++sink->pairingPrompts; });
        QObject::connect(raw, &BleSession::pairingFinished, raw,
                         [sink](const hio::Status& status) { sink->pairings.push_back(status); });
        QObject::connect(raw, &BleSession::linesDropped, raw,
                         [sink](quint64 count) { sink->dropped += count; });
        m_sessions.push_back(std::move(*session));
        return raw;
    }

    // Opens and connects (link 1).
    BleSession* connected(Events*& events, bool autoReconnect = false, std::uint16_t mtu = 247) {
        BleSession* session = openRecorded(options(autoReconnect), events);
        if (session == nullptr) {
            return nullptr;
        }
        EXPECT_TRUE(waitFor([this] { return driver()->count(&Driver::connects) >= 1; }));
        driver()->run([mtu](IBleListener& listener) {
            listener.onLinkConnecting(1, true);
            listener.onLinkReady(1, LinkReady{.mtu = mtu, .writeMode = WriteMode::WithResponse});
        });
        EXPECT_TRUE(waitFor([events] { return events->reached(ConnectionState::Connected); }));
        return session;
    }

    void data(const std::string& text) {
        driver()->run([text](IBleListener& listener) {
            listener.onLinkData(1, std::as_bytes(std::span(text.data(), text.size())));
        });
    }

private:
    QTemporaryDir m_dir;
    std::shared_ptr<Driver> m_driver = std::make_shared<Driver>();
    std::unique_ptr<SessionRegistry> m_registry;
    std::unique_ptr<hio::ble::BleHost> m_host;
    std::vector<std::unique_ptr<Events>> m_recorded;
    std::vector<std::unique_ptr<BleSession>> m_sessions;
};

TEST_F(BleSessionTest, OpenValidates) {
    BleSessionOptions noProfile = options();
    noProfile.profile = nullptr;
    EXPECT_EQ(open(noProfile).error().code, ErrorCode::UsageInvalidArgument);
    BleSessionOptions noId = options();
    noId.deviceId.clear();
    EXPECT_EQ(open(noId).error().code, ErrorCode::UsageInvalidArgument);
}

TEST_F(BleSessionTest, ConnectsWithTheMtuAndIdentity) {
    Events* events = nullptr;
    BleSession* session = connected(events);
    ASSERT_NE(session, nullptr);
    ASSERT_GE(events->connections.size(), 2U);
    EXPECT_EQ(events->connections.front().state, ConnectionState::Connecting);
    const ConnectionEvent& last = events->connections.back();
    EXPECT_EQ(last.transport, hio::device::Transport::Ble);
    EXPECT_EQ(last.mtu, std::optional<std::uint16_t>(247));
    EXPECT_EQ(session->mtu(), 247);
    const auto identity = session->identity();
    EXPECT_EQ(identity.transport, hio::device::Transport::Ble);
    EXPECT_EQ(identity.id, deviceId);
    EXPECT_EQ(identity.displayName, "CHESTER-ABC123");
    EXPECT_FALSE(session->capabilities().testFlag(hio::device::Capability::Reset));
    EXPECT_EQ(session->consoleTargetId(), "app");
    EXPECT_EQ(registry()->sessionFor(hio::device::BleKey{.id = deviceId}), session);
}

TEST_F(BleSessionTest, LinesPromptAndRouting) {
    Events* events = nullptr;
    ASSERT_NE(connected(events), nullptr);
    // Prompt split across notifications, with ANSI codes; '#' and level-tagged lines.
    data("hello\r\n# legacy log\n[00:00:01.000,000] <wrn> app: low\nbt_n");
    data("us:~$ ");
    ASSERT_TRUE(waitFor([events] { return events->prompts == 1; }));
    EXPECT_EQ(events->texts(Channel::Shell, Direction::Out), std::vector<std::string>{"hello"});
    EXPECT_EQ(events->texts(Channel::Log, Direction::Out),
              (std::vector<std::string>{"# legacy log", "[00:00:01.000,000] <wrn> app: low"}));
    EXPECT_EQ(events->lines.at(2).level, hio::device::LogLevel::Warning);
    data("\r\x1b[Kbt_nus:~$ ");
    ASSERT_TRUE(waitFor([events] { return events->prompts == 2; }));
    // UTF-8 split across notifications.
    data("\xc4");
    data("\x8d\n");
    ASSERT_TRUE(waitFor([events] { return events->lines.size() >= 4; }));
    EXPECT_EQ(events->lines.back().text, "\xc4\x8d");
    for (const ConsoleLine& line : events->lines) {
        EXPECT_EQ(line.text.find("bt_nus"), std::string::npos) << "prompt emitted as a line";
    }
}

TEST_F(BleSessionTest, EchoOnlyAfterTheLastChunk) {
    Events* events = nullptr;
    BleSession* session = connected(events, false, 23);
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->sendLine(std::string(30, 'x')));
    ASSERT_TRUE(waitFor([this] { return driver()->written().size() == 1; }));
    const auto first = driver()->written().at(0);
    EXPECT_EQ(first.first.size(), 20U);
    driver()->run(
        [token = first.second](IBleListener& l) { l.onWriteDone(1, token, std::nullopt); });
    ASSERT_TRUE(waitFor([this] { return driver()->written().size() == 2; }));
    spin(50ms);
    EXPECT_TRUE(events->texts(Channel::Shell, Direction::In).empty());
    driver()->run([token = driver()->written().at(1).second](IBleListener& l) {
        l.onWriteDone(1, token, std::nullopt);
    });
    ASSERT_TRUE(
        waitFor([events] { return !events->texts(Channel::Shell, Direction::In).empty(); }));
    EXPECT_EQ(events->texts(Channel::Shell, Direction::In),
              std::vector<std::string>{std::string(30, 'x')});
}

TEST_F(BleSessionTest, TooLongLineIsAUsageError) {
    Events* events = nullptr;
    BleSession* session = connected(events);
    ASSERT_NE(session, nullptr);
    const auto sent = session->sendLine(std::string(hio::ble::NusLink::maxLineBytes + 1, 'x'));
    ASSERT_FALSE(sent);
    EXPECT_EQ(sent.error().code, ErrorCode::UsageInvalidArgument);
}

TEST_F(BleSessionTest, SendWhileConnectingIsNotConnectedWarning) {
    Events* events = nullptr;
    BleSession* session = openRecorded(options(), events);
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(waitFor([this] { return driver()->count(&Driver::connects) == 1; }));
    ASSERT_TRUE(session->sendLine("version"));
    ASSERT_TRUE(waitFor([events] { return !events->warnings.empty(); }));
    EXPECT_EQ(events->warnings.front().code, ErrorCode::TargetNotConnected);
}

TEST_F(BleSessionTest, PairingPromptWhileTheCccdIsPending) {
    Events* events = nullptr;
    ASSERT_NE(openRecorded(options(), events), nullptr);
    ASSERT_TRUE(waitFor([this] { return driver()->count(&Driver::connects) == 1; }));
    driver()->run([](IBleListener& l) { l.onLinkConnecting(1, true); });
    ASSERT_TRUE(waitFor([events] { return events->pairingPrompts == 1; }));
    EXPECT_FALSE(events->reached(ConnectionState::Connected));
}

TEST_F(BleSessionTest, NotPairedIsFatalEvenWithAutoReconnect) {
    Events* events = nullptr;
    ASSERT_NE(openRecorded(options(true), events), nullptr);
    ASSERT_TRUE(waitFor([this] { return driver()->count(&Driver::connects) == 1; }));
    driver()->run([](IBleListener& l) {
        l.onLinkLost(1, hio::makeError(ErrorCode::BleNotPaired, "CCCD write rejected"));
    });
    ASSERT_TRUE(waitFor([events] { return events->reached(ConnectionState::Stopped); }));
    EXPECT_EQ(events->connections.back().error.value_or(hio::Error{}).code,
              ErrorCode::BleNotPaired);
}

TEST_F(BleSessionTest, FirstFailureWithoutAutoReconnectIsStopped) {
    Events* events = nullptr;
    ASSERT_NE(openRecorded(options(false), events), nullptr);
    ASSERT_TRUE(waitFor([this] { return driver()->count(&Driver::connects) == 1; }));
    driver()->run(
        [](IBleListener& l) { l.onLinkLost(1, hio::makeError(ErrorCode::BleConnectFailed)); });
    ASSERT_TRUE(waitFor([events] { return events->reached(ConnectionState::Stopped); }));
    EXPECT_EQ(events->connections.back().error.value_or(hio::Error{}).message,
              "Could not connect to CHESTER-ABC123 over Bluetooth.");
}

TEST_F(BleSessionTest, ReconnectsOnlyWhileTheDeviceIsSeen) {
    Events* events = nullptr;
    BleSession* session = connected(events, true);
    ASSERT_NE(session, nullptr);
    driver()->run(
        [](IBleListener& l) { l.onLinkLost(1, hio::makeError(ErrorCode::BleDisconnected)); });
    ASSERT_TRUE(waitFor([events] { return events->reached(ConnectionState::Disconnected); }));
    EXPECT_EQ(events->connections.back().error.value_or(hio::Error{}).message,
              "The Bluetooth connection to CHESTER-ABC123 was lost.");
    ASSERT_TRUE(
        waitFor([this] { return driver()->count(&Driver::scans) == 1; })); // background scan
    spin(400ms); // several retry intervals, but not seen: no retry
    EXPECT_EQ(driver()->count(&Driver::connects), 1);
    {
        const std::scoped_lock lock(driver()->mutex);
        driver()->nextConnect = LinkId{2};
    }
    driver()->run([](IBleListener& l) {
        l.onAdvertisement(hio::ble::Advertisement{.id = deviceId,
                                                  .name = "CHESTER-ABC123",
                                                  .rssi = -61,
                                                  .serviceUuids = {},
                                                  .seen = std::chrono::steady_clock::now()});
    });
    ASSERT_TRUE(waitFor([this] { return driver()->count(&Driver::connects) == 2; }));
    EXPECT_EQ(session->rssi(), std::optional<int>(-61));
    // An automatic attempt does not report Connecting; the connect ends the background scan.
    driver()->run([](IBleListener& l) {
        l.onLinkReady(2, LinkReady{.mtu = 185, .writeMode = WriteMode::WithResponse});
    });
    ASSERT_TRUE(
        waitFor([session] { return session->connection().state == ConnectionState::Connected; }));
    ASSERT_TRUE(waitFor([this] { return driver()->count(&Driver::stopScans) >= 1; }));
}

TEST_F(BleSessionTest, ExplicitReconnectAlwaysTries) {
    Events* events = nullptr;
    BleSession* session = openRecorded(options(false), events);
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(waitFor([this] { return driver()->count(&Driver::connects) == 1; }));
    driver()->run(
        [](IBleListener& l) { l.onLinkLost(1, hio::makeError(ErrorCode::BleConnectFailed)); });
    ASSERT_TRUE(waitFor([events] { return events->reached(ConnectionState::Stopped); }));
    ASSERT_TRUE(session->reconnect());
    ASSERT_TRUE(waitFor([this] { return driver()->count(&Driver::connects) == 2; }));
    EXPECT_EQ(events->connections.back().state, ConnectionState::Connecting);
}

TEST_F(BleSessionTest, SameFailureIsReportedOnce) {
    Events* events = nullptr;
    BleSession* session = connected(events, true);
    ASSERT_NE(session, nullptr);
    driver()->run(
        [](IBleListener& l) { l.onLinkLost(1, hio::makeError(ErrorCode::BleDisconnected)); });
    ASSERT_TRUE(waitFor([events] { return events->reached(ConnectionState::Disconnected); }));
    const std::size_t count = events->connections.size();
    for (int i = 0; i < 3; ++i) {
        driver()->run([](IBleListener& l) {
            l.onAdvertisement(hio::ble::Advertisement{.id = deviceId,
                                                      .name = {},
                                                      .rssi = -60,
                                                      .serviceUuids = {},
                                                      .seen = std::chrono::steady_clock::now()});
        });
        const int before = driver()->count(&Driver::connects);
        ASSERT_TRUE(waitFor([&] { return driver()->count(&Driver::connects) > before; }));
        driver()->run(
            [](IBleListener& l) { l.onLinkLost(1, hio::makeError(ErrorCode::BleDisconnected)); });
        spin(30ms);
    }
    EXPECT_EQ(events->connections.size(), count);
}

TEST_F(BleSessionTest, CloseDuringConnectCancelsTheLink) {
    Events* events = nullptr;
    BleSession* session = openRecorded(options(), events);
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(waitFor([this] { return driver()->count(&Driver::connects) == 1; }));
    driver()->run([](IBleListener& l) { l.onLinkConnecting(1, true); }); // CCCD pending
    session->close(); // returns at once; the BLE thread disconnects (plan 0011, 2026-10-19)
    EXPECT_EQ(registry()->sessionCount(), 0U);
    ASSERT_TRUE(waitFor([this] { return driver()->count(&Driver::disconnects) == 1; }));
    EXPECT_FALSE(registry()->bleInUse(deviceId).otherProcess.has_value());
    EXPECT_FALSE(session->sendLine("x"));
}

TEST_F(BleSessionTest, PairResultsArrive) {
    Events* events = nullptr;
    BleSession* session = connected(events);
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->pair());
    spin(50ms);
    // The mock's isPaired() is false and requestPairing() succeeds: the result comes later.
    EXPECT_TRUE(events->pairings.empty());
    driver()->run([](IBleListener& l) {
        l.onPairingFinished(deviceId, hio::fail(ErrorCode::BlePairingFailed));
    });
    ASSERT_TRUE(waitFor([events] { return events->pairings.size() == 1; }));
    EXPECT_FALSE(events->pairings.front().has_value());
    ASSERT_FALSE(events->warnings.empty());
    EXPECT_EQ(events->warnings.back().code, ErrorCode::BlePairingFailed);
}

TEST_F(BleSessionTest, EventOverflowIsReportedAsDroppedLines) {
    Events* events = nullptr;
    ASSERT_NE(connected(events), nullptr);
    std::string many;
    for (std::size_t i = 0; i < BleSession::eventQueueCapacity + 50; ++i) {
        many += "l\n";
    }
    const int before = driver()->count(&Driver::actionsDone);
    data(many);
    // The owner thread does not drain meanwhile (no event loop): the queue overflows on the BLE
    // thread. Waits for the action to finish instead of a fixed time (review B12).
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    while (driver()->count(&Driver::actionsDone) == before &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    ASSERT_GT(driver()->count(&Driver::actionsDone), before);
    ASSERT_TRUE(waitFor([events] { return events->dropped >= 50; }));
    EXPECT_LE(events->lines.size(), BleSession::eventQueueCapacity);
}

TEST_F(BleSessionTest, ConsoleFileWithConnectionLines) {
    Events* events = nullptr;
    BleSessionOptions opts = options();
    opts.consoleFile = std::nullopt; // the default file
    BleSession* session = openRecorded(std::move(opts), events);
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(waitFor([this] { return driver()->count(&Driver::connects) == 1; }));
    driver()->run([](IBleListener& l) {
        l.onLinkReady(1, LinkReady{.mtu = 247, .writeMode = WriteMode::WithResponse});
    });
    data("hello\nbt_nus:~$ ");
    ASSERT_TRUE(waitFor([events] { return events->prompts == 1; }));
    const fs::path file = session->consoleFile();
    ASSERT_FALSE(file.empty());
    EXPECT_TRUE(file.filename().string().starts_with("chester-ble-"));
    EXPECT_TRUE(file.filename().string().ends_with("-F1-22-33-44-55-66.log"));
    session->close();
    stopHost(); // the BLE thread closed the file (close() does not wait)
    std::ifstream input(file);
    std::stringstream content;
    content << input.rdbuf();
    const std::string text = content.str();
    EXPECT_NE(text.find("Console: CHESTER, Bluetooth F1:22:33:44:55:66 (CHESTER-ABC123)"),
              std::string::npos);
    EXPECT_NE(text.find(" @ connected"), std::string::npos);
    EXPECT_NE(text.find(" > hello"), std::string::npos);
    EXPECT_NE(text.find(" @ stopped"), std::string::npos);
}

// close() does not wait (plan 0011 decisions, 2026-10-19): destroying the session right after it
// is safe, and the BLE thread still writes the last line and closes the file.
TEST_F(BleSessionTest, CloseAndImmediateDestructionStillClosesTheFile) {
    Events* events = nullptr;
    BleSessionOptions opts = options();
    opts.consoleFile = std::nullopt; // the default file
    BleSession* session = openRecorded(std::move(opts), events);
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(waitFor([this] { return driver()->count(&Driver::connects) == 1; }));
    driver()->run([](IBleListener& l) {
        l.onLinkReady(1, LinkReady{.mtu = 247, .writeMode = WriteMode::WithResponse});
    });
    data("last words\nbt_nus:~$ ");
    ASSERT_TRUE(waitFor([events] { return events->prompts == 1; }));
    const fs::path file = session->consoleFile();
    ASSERT_FALSE(file.empty());
    const auto started = std::chrono::steady_clock::now();
    dropLastSession(); // close() + destruction
    EXPECT_LT(std::chrono::steady_clock::now() - started, 500ms);
    EXPECT_EQ(registry()->sessionCount(), 0U);
    ASSERT_TRUE(waitFor([this] { return driver()->count(&Driver::disconnects) == 1; }));
    stopHost();
    std::ifstream input(file);
    std::stringstream content;
    content << input.rdbuf();
    const std::string text = content.str();
    const auto words = text.find(" > last words");
    ASSERT_NE(words, std::string::npos) << text;
    EXPECT_NE(text.find(" @ stopped", words), std::string::npos) << text;
}

// A new session of the same device right after close(): no lock clash ("already used"), the old
// link is disconnected before the new one connects, and both sessions' lines reach a console file
// (the same default file name within one second is appended to after the old file was closed).
TEST_F(BleSessionTest, ReopenRightAfterCloseIsSafe) {
    Events* first = nullptr;
    BleSessionOptions opts = options();
    opts.consoleFile = std::nullopt;
    BleSession* session = openRecorded(opts, first);
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(waitFor([this] { return driver()->count(&Driver::connects) == 1; }));
    driver()->run([](IBleListener& l) {
        l.onLinkReady(1, LinkReady{.mtu = 247, .writeMode = WriteMode::WithResponse});
    });
    data("first\n");
    ASSERT_TRUE(waitFor([first] { return !first->lines.empty(); }));
    driver()->setNextConnect(2);
    dropLastSession(); // close() + destruction, no wait
    Events* second = nullptr;
    BleSession* again = openRecorded(opts, second);
    ASSERT_NE(again, nullptr) << "the same device right after close() must open";
    ASSERT_TRUE(waitFor([this] { return driver()->count(&Driver::connects) == 2; }));
    EXPECT_EQ(driver()->history(),
              (std::vector<std::string>{"connect 1", "disconnect 1", "connect 2"}));
    driver()->run([](IBleListener& l) {
        l.onLinkReady(2, LinkReady{.mtu = 247, .writeMode = WriteMode::WithResponse});
    });
    driver()->run([](IBleListener& l) {
        const std::string text = "second\n";
        l.onLinkData(2, std::as_bytes(std::span(text.data(), text.size())));
    });
    ASSERT_TRUE(waitFor([second] { return !second->lines.empty(); }));
    EXPECT_EQ(second->lines.front().text, "second");
    dropLastSession();
    stopHost();
    std::string all;
    for (const auto& entry : fs::directory_iterator(root() / "console")) {
        std::ifstream input(entry.path());
        std::stringstream content;
        content << input.rdbuf();
        const std::string text = content.str();
        const auto firstAt = text.find(" > first");
        if (firstAt != std::string::npos) {
            // The old session's file was finished before anything of the new one.
            const auto stopped = text.find(" @ stopped", firstAt);
            ASSERT_NE(stopped, std::string::npos) << text;
            const auto secondAt = text.find(" > second");
            EXPECT_TRUE(secondAt == std::string::npos || secondAt > stopped) << text;
        }
        all += text;
    }
    EXPECT_NE(all.find(" > first"), std::string::npos);
    EXPECT_NE(all.find(" > second"), std::string::npos);
}

// Review B2: a background scan that fails (here not an adapter error) is reported once and
// restarted only by the retry timer, paced by the reconnect interval (no spin).
TEST_F(BleSessionTest, BackgroundScanErrorIsShownOnceAndPaced) {
    Events* events = nullptr;
    BleSession* session = connected(events, true);
    ASSERT_NE(session, nullptr);
    {
        const std::scoped_lock lock(driver()->mutex);
        driver()->scanError = ErrorCode::Timeout;
    }
    driver()->run(
        [](IBleListener& l) { l.onLinkLost(1, hio::makeError(ErrorCode::BleDisconnected)); });
    ASSERT_TRUE(waitFor([events] { return events->reached(ConnectionState::Disconnected); }));
    const auto started = std::chrono::steady_clock::now();
    ASSERT_TRUE(waitFor([this] { return driver()->count(&Driver::scans) >= 3; }));
    const auto elapsed = std::chrono::steady_clock::now() - started;
    // 3 starts need at least 2 reconnect intervals (100 ms in these timings).
    EXPECT_GE(elapsed, 180ms);
    const auto timeouts = std::ranges::count_if(
        events->warnings, [](const hio::Error& error) { return error.code == ErrorCode::Timeout; });
    EXPECT_EQ(timeouts, 1);
    EXPECT_EQ(session->connection().state, ConnectionState::Disconnected);
}

// Review B2: an adapter that cannot be used during auto-reconnect ends the session (Q9).
TEST_F(BleSessionTest, BackgroundScanAdapterErrorStops) {
    Events* events = nullptr;
    BleSession* session = connected(events, true);
    ASSERT_NE(session, nullptr);
    {
        const std::scoped_lock lock(driver()->mutex);
        driver()->scanError = ErrorCode::BleAdapterOff;
    }
    driver()->run(
        [](IBleListener& l) { l.onLinkLost(1, hio::makeError(ErrorCode::BleDisconnected)); });
    ASSERT_TRUE(waitFor([events] { return events->reached(ConnectionState::Stopped); }));
    EXPECT_EQ(events->connections.back().error.value_or(hio::Error{}).code,
              ErrorCode::BleAdapterOff);
    spin(300ms);
    EXPECT_EQ(driver()->count(&Driver::scans), 1); // not restarted
}

// Review B4: synchronous back-pressure - at most maxLinesInFlight lines wait; the caller gets
// internal.queue_full and sends again later instead of losing lines.
TEST_F(BleSessionTest, LinesInFlightAreBounded) {
    Events* events = nullptr;
    BleSession* session = connected(events);
    ASSERT_NE(session, nullptr);
    std::size_t accepted = 0;
    std::optional<hio::Error> refused;
    for (std::size_t i = 0; i < BleSession::maxLinesInFlight + 10 && !refused; ++i) {
        if (hio::Status sent = session->sendLine("x" + std::to_string(i)); sent) {
            ++accepted;
        } else {
            refused = sent.error();
        }
    }
    EXPECT_EQ(accepted, BleSession::maxLinesInFlight);
    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused.value_or(hio::Error{}).code, ErrorCode::InternalQueueFull);
    // The first write is confirmed: its echo frees one place.
    ASSERT_TRUE(waitFor([this] { return !driver()->written().empty(); }));
    driver()->run([token = driver()->written().front().second](IBleListener& l) {
        l.onWriteDone(1, token, std::nullopt);
    });
    ASSERT_TRUE(waitFor([&] { return session->sendLine("again").has_value(); }));
    EXPECT_FALSE(session->sendLine("full again").has_value());
    // A lost link rejects every waiting line (warnings), which frees all places.
    driver()->run(
        [](IBleListener& l) { l.onLinkLost(1, hio::makeError(ErrorCode::BleDisconnected)); });
    ASSERT_TRUE(waitFor([&] { return session->sendLine("after").has_value(); }));
}

// Review B6: the device id is canonical: another spelling is the same session, lock and file.
TEST_F(BleSessionTest, DeviceIdIsCanonical) {
    Events* events = nullptr;
    BleSession* session = openRecorded(options(false, "f1:22:33:44:55:66"), events);
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->identity().id, deviceId);
    const auto again = open(options(false, " F1:22:33:44:55:66 "));
    ASSERT_FALSE(again);
    EXPECT_EQ(again.error().code, ErrorCode::BleInUseByOtherProcess);
    EXPECT_TRUE(fs::exists(root() / "locks" / "ble-F1-22-33-44-55-66.lock"));
}

// ---- registry (B2)
// ---------------------------------------------------------------------------------

TEST_F(BleSessionTest, SameDeviceTwiceIsAlreadyUsed) {
    Events* events = nullptr;
    ASSERT_NE(openRecorded(options(), events), nullptr);
    const auto second = open(options());
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error().code, ErrorCode::BleInUseByOtherProcess);
    EXPECT_EQ(second.error().message,
              "The device F1:22:33:44:55:66 is already used by this application.");
    const hio::device::BleUse use = registry()->bleInUse(deviceId);
    EXPECT_NE(use.session, nullptr);
}

TEST_F(BleSessionTest, JLinkAndBleShareTheSessionLimit) {
    Events* events = nullptr;
    for (std::size_t i = 0; i < SessionRegistry::maxSessions; ++i) {
        ASSERT_NE(openRecorded(options(false, "F1:00:00:00:00:0" + std::to_string(i)), events),
                  nullptr);
    }
    const auto ble = open(options(false, "F1:00:00:00:00:99"));
    ASSERT_FALSE(ble);
    EXPECT_EQ(ble.error().code, ErrorCode::InternalSessionLimit);
    const auto probe = registry()->checkAvailable(std::optional<std::uint32_t>(1234));
    ASSERT_FALSE(probe);
    EXPECT_EQ(probe.error().code, ErrorCode::InternalSessionLimit);
    dropLastSession();
    EXPECT_TRUE(registry()->checkAvailable(std::optional<std::uint32_t>(1234)));
}

} // namespace
