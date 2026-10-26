// Discovery tools against the GMock provider (plan/features/0013 "Tests: Tools"): list_devices
// (golden JSON), refresh, disconnect, connect (consent, already open, provider errors, the first
// attempt's outcome).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/probe_lock.h"
#include "hio/device/session_events.h"
#include "hio/mcp/consent.h"
#include "hio/mcp/tools/device_provider.h"
#include "tools_test_support.h"

#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringLiteral>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <expected>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::mcp::tools::test {
namespace {

using namespace std::chrono_literals;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

QJsonObject golden(const char* name) {
    QFile file(QStringLiteral(HIO_MCP_TOOLS_GOLDEN_DIR "/%1").arg(QString::fromLatin1(name)));
    if (!file.open(QIODevice::ReadOnly)) {
        ADD_FAILURE() << "missing golden file " << name;
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

std::vector<DeviceEntry> sampleEntries() {
    hio::device::ConnectionEvent connected;
    connected.state = hio::device::ConnectionState::Connected;
    connected.targetMillivolts = 3300;
    return {DeviceEntry{.id = "jlink:801000001",
                        .kind = "jlink",
                        .displayName = "J-Link 801000001",
                        .profileId = "chester",
                        .state = DeviceEntry::State::Open,
                        .owner = std::nullopt,
                        .connection = connected,
                        .openedByMcp = true},
            DeviceEntry{.id = "jlink:801000002",
                        .kind = "jlink",
                        .displayName = "J-Link 801000002",
                        .profileId = {},
                        .state = DeviceEntry::State::Available,
                        .owner = std::nullopt,
                        .connection = std::nullopt,
                        .openedByMcp = false},
            DeviceEntry{.id = "jlink:801000003",
                        .kind = "jlink",
                        .displayName = "J-Link 801000003",
                        .profileId = {},
                        .state = DeviceEntry::State::InUseByOtherProcess,
                        .owner = hio::device::ProbeLockOwner{.app = "hio-monitor",
                                                             .pid = 4242,
                                                             .lockedAt = std::nullopt},
                        .connection = std::nullopt,
                        .openedByMcp = false}};
}

TEST(DiscoveryTools, ListDevicesGolden) {
    NiceMock<MockDeviceProvider> provider;
    ON_CALL(provider, list()).WillByDefault(Return(sampleEntries()));
    EXPECT_CALL(provider, refresh(_)).Times(0);
    Harness harness(provider);
    auto call = harness.call(QStringLiteral("list_devices"));
    ASSERT_FALSE(call.isError()) << call.errorCode();
    EXPECT_EQ(call.structured(), golden("list_devices.json"));
    // The text block is the compact JSON of the structured content.
    EXPECT_EQ(QJsonDocument::fromJson(call.result()
                                          .value(QStringLiteral("content"))
                                          .toArray()
                                          .at(0)
                                          .toObject()
                                          .value(QStringLiteral("text"))
                                          .toString()
                                          .toUtf8())
                  .object(),
              call.structured());
}

// ADR-0162: refresh=true waits for the provider's asynchronous refresh, then lists the cache.
TEST(DiscoveryTools, ListDevicesRefresh) {
    NiceMock<MockDeviceProvider> provider;
    std::vector<DeviceEntry> cache;
    ON_CALL(provider, list()).WillByDefault([&cache] { return cache; });
    std::function<void(hio::Status)> pending;
    EXPECT_CALL(provider, refresh(_)).WillOnce([&pending](std::function<void(hio::Status)> done) {
        pending = std::move(done);
    });
    Harness harness(provider);
    auto call = harness.start(QStringLiteral("list_devices"), args({{"refresh", true}}));
    runFor(std::chrono::milliseconds(50));
    EXPECT_FALSE(call.done()); // still waiting for the enumeration
    ASSERT_TRUE(pending);
    cache = sampleEntries(); // the provider updates its cache before `done`
    pending(hio::Status{});
    ASSERT_TRUE(runUntil([&call] { return call.done(); }));
    EXPECT_FALSE(call.isError());
    EXPECT_EQ(call.structured(), golden("list_devices.json"));

    EXPECT_CALL(provider, refresh(_)).WillOnce([](const std::function<void(hio::Status)>& done) {
        done(std::unexpected(makeError(ErrorCode::DependencyJLinkNotFound)));
    });
    auto failed = harness.call(QStringLiteral("list_devices"), args({{"refresh", true}}));
    EXPECT_EQ(failed.errorCode(), "dependency.jlink_not_found");
}

// Review V1: while the first (start-up) enumeration runs, list_devices without refresh waits for
// it through refresh() (which joins it) and lists the cache, even after a failed enumeration;
// afterwards it lists the cache at once without refreshing again.
TEST(DiscoveryTools, ListDevicesWaitsForTheFirstScan) {
    NiceMock<MockDeviceProvider> provider;
    std::vector<DeviceEntry> cache;
    ON_CALL(provider, list()).WillByDefault([&cache] { return cache; });
    bool pendingScan = true;
    ON_CALL(provider, firstScanPending()).WillByDefault([&pendingScan] { return pendingScan; });
    std::function<void(hio::Status)> pending;
    EXPECT_CALL(provider, refresh(_)).WillOnce([&pending](std::function<void(hio::Status)> done) {
        pending = std::move(done);
    });
    Harness harness(provider);
    auto call = harness.start(QStringLiteral("list_devices"));
    runFor(std::chrono::milliseconds(50));
    EXPECT_FALSE(call.done()); // waits for the start-up enumeration
    ASSERT_TRUE(pending);
    cache = sampleEntries();
    pendingScan = false;
    pending(hio::Status{});
    ASSERT_TRUE(runUntil([&call] { return call.done(); }));
    EXPECT_FALSE(call.isError());
    EXPECT_EQ(call.structured(), golden("list_devices.json"));

    // Afterwards: the cache at once, no second enumeration (refresh() expected once above).
    auto second = harness.call(QStringLiteral("list_devices"));
    EXPECT_FALSE(second.isError());
    EXPECT_EQ(second.structured(), golden("list_devices.json"));
    ::testing::Mock::VerifyAndClearExpectations(&provider);

    // A failed first enumeration still gives the (cached) list, not an error.
    pendingScan = true;
    EXPECT_CALL(provider, refresh(_))
        .WillOnce([&pendingScan](const std::function<void(hio::Status)>& done) {
            pendingScan = false;
            done(std::unexpected(makeError(ErrorCode::DependencyJLinkNotFound)));
        });
    auto failed = harness.call(QStringLiteral("list_devices"));
    EXPECT_FALSE(failed.isError()) << failed.errorCode();
    EXPECT_EQ(failed.structured(), golden("list_devices.json"));
}

// A refresh that answers after the call was cancelled is ignored (the call ended cancelled).
TEST(DiscoveryTools, ListDevicesRefreshCancelled) {
    NiceMock<MockDeviceProvider> provider;
    ON_CALL(provider, list()).WillByDefault(Return(std::vector<DeviceEntry>{}));
    std::function<void(hio::Status)> pending;
    EXPECT_CALL(provider, refresh(_)).WillOnce([&pending](std::function<void(hio::Status)> done) {
        pending = std::move(done);
    });
    Harness harness(provider);
    auto call = harness.start(QStringLiteral("list_devices"), args({{"refresh", true}}));
    harness.cancel(harness.lastId());
    runFor(std::chrono::milliseconds(50));
    ASSERT_TRUE(pending);
    pending(hio::Status{}); // must not crash or answer twice
    runFor(std::chrono::milliseconds(50));
}

TEST(DiscoveryTools, Disconnect) {
    NiceMock<MockDeviceProvider> provider;
    FakeConsoleSession session;
    ON_CALL(provider, list()).WillByDefault(Return(sampleEntries()));
    ON_CALL(provider, find(_))
        .WillByDefault(Return(DeviceRef{.console = &session, .jlink = nullptr}));
    EXPECT_CALL(provider, close(std::string_view("jlink:801000001"), _))
        .WillOnce(Return(hio::Status{}));
    Harness harness(provider);
    auto call = harness.call(QStringLiteral("disconnect"));
    ASSERT_FALSE(call.isError()) << call.errorCode();
    EXPECT_TRUE(call.structured().value(QStringLiteral("closed")).toBool());

    EXPECT_CALL(provider, close(_, _))
        .WillOnce(Return(hio::Status(std::unexpected(makeError(ErrorCode::McpToolNotAllowed)))));
    auto refused =
        harness.call(QStringLiteral("disconnect"), args({{"device", "jlink:801000001"}}));
    EXPECT_EQ(refused.errorCode(), "mcp.tool_not_allowed");
}

TEST(DiscoveryTools, ConnectAsksConsentAndWaitsForTheFirstAttempt) {
    NiceMock<MockDeviceProvider> provider;
    FakeConsoleSession session;
    session.setConnection(hio::device::ConnectionState::Connecting);
    std::optional<OpenRequest> requested;
    EXPECT_CALL(provider, find(_)).WillOnce(Return(std::nullopt));
    EXPECT_CALL(provider, open(_, _))
        .WillOnce([&](const OpenRequest& request,
                      const std::function<void(hio::Result<DeviceRef>)>& done) {
            requested = request;
            done(DeviceRef{.console = &session, .jlink = nullptr});
        });
    Harness harness(provider);
    auto call = harness.start(
        QStringLiteral("connect"),
        args({{"device", "jlink:801000001"}, {"speed_khz", 4000}, {"auto_reconnect", false}}));
    ASSERT_EQ(harness.consent.requests.size(), 1U);
    EXPECT_EQ(harness.consent.requests[0].categories,
              hio::mcp::ConsentCategories(hio::mcp::ConsentCategory::Connect));
    EXPECT_EQ(harness.consent.requests[0].deviceId, "jlink:801000001");
    ASSERT_TRUE(requested.has_value());
    const OpenRequest request = requested.value_or(OpenRequest{});
    EXPECT_EQ(request.speedKhz, std::optional<int>(4000));
    EXPECT_EQ(request.autoReconnect, std::optional<bool>(false));
    EXPECT_EQ(request.context.client.name, "tester");
    runFor(30ms);
    EXPECT_FALSE(call.done()); // still Connecting
    session.setConnection(hio::device::ConnectionState::Connected);
    ASSERT_TRUE(runUntil([&] { return call.done(); }));
    const QJsonObject result = call.structured();
    EXPECT_FALSE(result.value(QStringLiteral("already_open")).toBool());
    EXPECT_EQ(result.value(QStringLiteral("connection")).toObject().value(QStringLiteral("state")),
              QStringLiteral("connected"));
}

TEST(DiscoveryTools, ConnectAlreadyOpenAndDenied) {
    NiceMock<MockDeviceProvider> provider;
    FakeConsoleSession session;
    EXPECT_CALL(provider, find(std::string_view("jlink:1")))
        .WillOnce(Return(DeviceRef{.console = &session, .jlink = nullptr}));
    EXPECT_CALL(provider, open(_, _)).Times(0);
    Harness harness(provider);
    auto open = harness.call(QStringLiteral("connect"), args({{"device", "jlink:1"}}));
    EXPECT_TRUE(open.structured().value(QStringLiteral("already_open")).toBool());
    EXPECT_TRUE(harness.consent.requests.empty());

    EXPECT_CALL(provider, find(std::string_view("jlink:2"))).WillOnce(Return(std::nullopt));
    harness.consent.mode = hio::mcp::test::FakeConsentBroker::Mode::Deny;
    auto denied = harness.call(QStringLiteral("connect"), args({{"device", "jlink:2"}}));
    EXPECT_EQ(denied.errorCode(), "mcp.tool_not_allowed");

    auto invalid = harness.call(QStringLiteral("connect"), args({{"device", "usb:1"}}));
    EXPECT_EQ(invalid.rpcError(), -32602);
}

TEST(DiscoveryTools, ConnectFailureClosesTheSession) {
    NiceMock<MockDeviceProvider> provider;
    FakeConsoleSession session;
    session.setConnection(hio::device::ConnectionState::Connecting);
    EXPECT_CALL(provider, find(_)).WillOnce(Return(std::nullopt));
    EXPECT_CALL(provider, open(_, _))
        .WillOnce([&](const OpenRequest&, const std::function<void(hio::Result<DeviceRef>)>& done) {
            done(DeviceRef{.console = &session, .jlink = nullptr});
        });
    EXPECT_CALL(provider, close(std::string_view("jlink:1"), _)).WillOnce(Return(hio::Status{}));
    Harness harness(provider);
    auto call = harness.start(QStringLiteral("connect"), args({{"device", "jlink:1"}}));
    session.setConnection(hio::device::ConnectionState::Stopped,
                          makeError(ErrorCode::ProbeNotFound));
    ASSERT_TRUE(runUntil([&] { return call.done(); }));
    EXPECT_EQ(call.errorCode(), "probe.not_found");
    runFor(50ms); // the session is closed from the event loop
}

TEST(DiscoveryTools, ConnectProviderError) {
    NiceMock<MockDeviceProvider> provider;
    EXPECT_CALL(provider, find(_)).WillOnce(Return(std::nullopt));
    EXPECT_CALL(provider, open(_, _))
        .WillOnce([](const OpenRequest&, const std::function<void(hio::Result<DeviceRef>)>& done) {
            done(std::unexpected(makeError(ErrorCode::InternalSessionLimit)));
        });
    Harness harness(provider);
    auto call = harness.call(QStringLiteral("connect"), args({{"device", "jlink:1"}}));
    EXPECT_EQ(call.errorCode(), "internal.session_limit");
}

} // namespace
} // namespace hio::mcp::tools::test
