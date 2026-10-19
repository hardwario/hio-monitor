// BleHost with a GMock IBleApi on its thread (plan/features/0011 Tests "Host (TSan)"): concurrent
// post/stop, stop with 8 links, queue full, the API created and destroyed on the BLE thread,
// adapter queries, the event channel bound.

#include "hio/core/bounded_queue.h"
#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/hw/ble/ble_api.h"
#include "hio/hw/ble/ble_host.h"
#include "hio/hw/ble/event_channel.h"
#include "hio/hw/ble/link_handler.h"
#include "hio/hw/ble/types.h"
#include "mock_ble_api.h"

#include <QThread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

using hio::ErrorCode;
using hio::ble::BleHost;
using hio::ble::LinkId;
using hio::ble::test::MockBleApi;
using hio::ble::test::readyAdapter;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using namespace std::chrono_literals;

// What the API saw, shared with the test thread (mutex: the API runs on the BLE thread).
struct ApiRecord {
    std::mutex mutex;
    QThread* createdOn = nullptr;
    QThread* destroyedOn = nullptr;
    int connects = 0;
    int disconnects = 0;
};

class RecordedApi : public NiceMock<MockBleApi> {
public:
    explicit RecordedApi(std::shared_ptr<ApiRecord> record) : m_record(std::move(record)) {
        const std::scoped_lock lock(m_record->mutex);
        m_record->createdOn = QThread::currentThread();
        ON_CALL(*this, adapter()).WillByDefault(Return(readyAdapter()));
        ON_CALL(*this, connect(_, _)).WillByDefault([this](const auto&, const auto&) {
            const std::scoped_lock guard(m_record->mutex);
            return hio::Result<LinkId>(static_cast<LinkId>(++m_record->connects));
        });
        ON_CALL(*this, disconnect(_)).WillByDefault([this](LinkId) {
            const std::scoped_lock guard(m_record->mutex);
            ++m_record->disconnects;
        });
        ON_CALL(*this, startScan(_)).WillByDefault(Return(hio::Status{}));
    }
    RecordedApi(const RecordedApi&) = delete;
    RecordedApi& operator=(const RecordedApi&) = delete;
    RecordedApi(RecordedApi&&) = delete;
    RecordedApi& operator=(RecordedApi&&) = delete;
    ~RecordedApi() override {
        const std::scoped_lock lock(m_record->mutex);
        m_record->destroyedOn = QThread::currentThread();
    }

private:
    std::shared_ptr<ApiRecord> m_record;
};

// A thread-safe handler record (the handler runs on the BLE thread).
struct SyncLog {
    std::mutex mutex;
    std::vector<std::string> events;
    bool closed = false;

    void add(std::string event) {
        const std::scoped_lock lock(mutex);
        events.push_back(std::move(event));
    }
};

class SyncHandler : public hio::ble::ILinkHandler {
public:
    explicit SyncHandler(std::shared_ptr<SyncLog> log) : m_log(std::move(log)) {}
    void onConnecting(bool /*automatic*/) override { m_log->add("connecting"); }
    void onPairingPrompt() override { m_log->add("prompt"); }
    void onReady(hio::ble::LinkReady /*ready*/) override { m_log->add("ready"); }
    void onData(std::span<const std::byte> /*data*/) override {}
    void onLineWritten(std::string line) override { m_log->add("written " + line); }
    void onLineRejected(std::string line, hio::Error error) override {
        m_log->add("rejected " + std::string(hio::idOf(error.code)) + " " + line);
    }
    void onDown(hio::Error error) override {
        m_log->add("down " + std::string(hio::idOf(error.code)));
    }
    void onPairingFinished(hio::Status status, bool alreadyPaired) override {
        m_log->add(std::string("paired ") +
                   (status ? "ok" : std::string(hio::idOf(status.error().code))) +
                   (alreadyPaired ? " already" : ""));
    }
    void onFlush() override {}
    void onClosed() override {
        const std::scoped_lock lock(m_log->mutex);
        m_log->closed = true;
    }

private:
    std::shared_ptr<SyncLog> m_log;
};

BleHost::ApiFactory recordedFactory(const std::shared_ptr<ApiRecord>& record) {
    return [record]() -> std::unique_ptr<hio::ble::IBleApi> {
        return std::make_unique<RecordedApi>(record);
    };
}

bool waitFor(const std::function<bool()>& done, std::chrono::milliseconds timeout = 5s) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (!done()) {
        if (std::chrono::steady_clock::now() > end) {
            return false;
        }
        std::this_thread::sleep_for(2ms);
    }
    return true;
}

TEST(BleHost, ApiIsCreatedUsedAndDestroyedOnTheBleThread) {
    auto record = std::make_shared<ApiRecord>();
    {
        BleHost host(recordedFactory(record));
        const auto adapter = host.queryAdapter(2s);
        ASSERT_TRUE(adapter.has_value());
        EXPECT_EQ(adapter->state, hio::ble::AdapterState::Ready);
        EXPECT_EQ(adapter->name, "hci0");
    }
    const std::scoped_lock lock(record->mutex);
    ASSERT_NE(record->createdOn, nullptr);
    EXPECT_NE(record->createdOn, QThread::currentThread());
    EXPECT_EQ(record->destroyedOn, record->createdOn);
}

TEST(BleHost, NotStartedHostStopsAtOnce) {
    auto record = std::make_shared<ApiRecord>();
    BleHost host(recordedFactory(record));
    host.stop();
    const std::scoped_lock lock(record->mutex);
    EXPECT_EQ(record->createdOn, nullptr);
}

TEST(BleHost, PostAfterStopIsCancelled) {
    auto record = std::make_shared<ApiRecord>();
    BleHost host(recordedFactory(record));
    ASSERT_TRUE(host.queryAdapter(2s));
    host.stop();
    const hio::Status posted = host.post(hio::ble::SendLine{.client = 1, .line = "x"});
    ASSERT_FALSE(posted);
    EXPECT_EQ(posted.error().code, ErrorCode::Cancelled);
    EXPECT_FALSE(host.queryAdapter(100ms));
}

TEST(BleHost, FullCommandQueueRejects) {
    auto record = std::make_shared<ApiRecord>();
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    BleHost host([record, released]() -> std::unique_ptr<hio::ble::IBleApi> {
        released.wait(); // the worker does not exist yet: nothing is drained
        return std::make_unique<RecordedApi>(record);
    });
    std::size_t accepted = 0;
    std::optional<hio::Error> rejected;
    for (std::size_t i = 0; i < BleHost::commandQueueCapacity + 1; ++i) {
        const hio::Status posted =
            host.post(hio::ble::SendLine{.client = 99, .line = std::to_string(i)});
        if (posted) {
            ++accepted;
        } else {
            rejected = posted.error();
        }
    }
    EXPECT_EQ(accepted, BleHost::commandQueueCapacity);
    ASSERT_TRUE(rejected.has_value());
    EXPECT_EQ(rejected.value_or(hio::Error{}).code, ErrorCode::InternalQueueFull);
    release.set_value();
    // Drained, it works again.
    EXPECT_TRUE(waitFor([&host] { return host.queryAdapter(100ms).has_value(); }));
}

TEST(BleHost, StopClosesEightLinksAndRejectsANinth) {
    auto record = std::make_shared<ApiRecord>();
    std::vector<std::shared_ptr<SyncLog>> logs;
    {
        BleHost host(recordedFactory(record));
        for (std::size_t i = 0; i < BleHost::maxLinks; ++i) {
            auto log = std::make_shared<SyncLog>();
            logs.push_back(log);
            ASSERT_TRUE(
                host.post(hio::ble::OpenLink{.client = host.newClientId(),
                                             .device = "F1:00:00:00:00:0" + std::to_string(i),
                                             .uuids = {},
                                             .handler = std::make_unique<SyncHandler>(log),
                                             .timing = {}}));
        }
        // B3: no reserved close slot is left for a ninth link.
        const hio::Status ninth = host.post(hio::ble::OpenLink{
            .client = host.newClientId(),
            .device = "F1:00:00:00:00:99",
            .uuids = {},
            .handler = std::make_unique<SyncHandler>(std::make_shared<SyncLog>()),
            .timing = {}});
        ASSERT_FALSE(ninth);
        EXPECT_EQ(ninth.error().code, ErrorCode::InternalSessionLimit);
        ASSERT_TRUE(waitFor([&] {
            const std::scoped_lock lock(record->mutex);
            return std::cmp_equal(record->connects, BleHost::maxLinks);
        }));
    } // ~BleHost: stop()
    const std::scoped_lock lock(record->mutex);
    EXPECT_EQ(record->disconnects, static_cast<int>(BleHost::maxLinks));
    for (const auto& log : logs) {
        const std::scoped_lock guard(log->mutex);
        EXPECT_TRUE(log->closed);
    }
}

// Review B3: a close uses the space its OpenLink reserved - also when the command queue is full -
// and frees the slot for a new link.
TEST(BleHost, CloseLinkIsNeverRefusedWhenTheCommandQueueIsFull) {
    auto record = std::make_shared<ApiRecord>();
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    BleHost host([record, released]() -> std::unique_ptr<hio::ble::IBleApi> {
        released.wait(); // nothing is drained until released
        return std::make_unique<RecordedApi>(record);
    });
    auto log = std::make_shared<SyncLog>();
    const auto client = host.newClientId();
    ASSERT_TRUE(host.post(hio::ble::OpenLink{.client = client,
                                             .device = "F1:22:33:44:55:66",
                                             .uuids = {},
                                             .handler = std::make_unique<SyncHandler>(log),
                                             .timing = {}}));
    hio::Status filled;
    for (std::size_t i = 0; i < BleHost::commandQueueCapacity && filled; ++i) {
        filled = host.post(hio::ble::SendLine{.client = client, .line = "x"});
    }
    ASSERT_FALSE(filled); // the command queue is full now
    EXPECT_EQ(filled.error().code, ErrorCode::InternalQueueFull);
    auto done = std::make_shared<hio::BoundedQueue<bool>>(1, hio::OverflowPolicy::Reject);
    ASSERT_TRUE(host.post(hio::ble::CloseLink{.client = client, .done = done}));
    release.set_value();
    ASSERT_TRUE(done->pop({}, 10s).has_value());
    {
        const std::scoped_lock lock(log->mutex);
        EXPECT_TRUE(log->closed);
    }
    // Eight new links fit again (the closed one's slot was released).
    for (std::size_t i = 0; i < BleHost::maxLinks; ++i) {
        EXPECT_TRUE(waitFor([&host, i] {
            return host
                .post(hio::ble::OpenLink{
                    .client = host.newClientId(),
                    .device = "F1:00:00:00:00:0" + std::to_string(i),
                    .uuids = {},
                    .handler = std::make_unique<SyncHandler>(std::make_shared<SyncLog>()),
                    .timing = {}})
                .has_value();
        })) << i;
    }
}

// Review B3: a close that overtakes its OpenLink still closes the link.
TEST(BleHost, CloseBeforeItsOpenIsProcessedClosesTheLink) {
    auto record = std::make_shared<ApiRecord>();
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    BleHost host([record, released]() -> std::unique_ptr<hio::ble::IBleApi> {
        released.wait();
        return std::make_unique<RecordedApi>(record);
    });
    // The close path is drained after the commands, so make the open wait behind a full drain
    // budget: many commands, then the open, then its close.
    for (std::size_t i = 0; i + 1 < BleHost::commandQueueCapacity; ++i) {
        ASSERT_TRUE(host.post(hio::ble::SendLine{.client = 999, .line = "x"}));
    }
    auto log = std::make_shared<SyncLog>();
    const auto client = host.newClientId();
    ASSERT_TRUE(host.post(hio::ble::OpenLink{.client = client,
                                             .device = "F1:22:33:44:55:66",
                                             .uuids = {},
                                             .handler = std::make_unique<SyncHandler>(log),
                                             .timing = {}}));
    auto done = std::make_shared<hio::BoundedQueue<bool>>(1, hio::OverflowPolicy::Reject);
    ASSERT_TRUE(host.post(hio::ble::CloseLink{.client = client, .done = done}));
    release.set_value();
    ASSERT_TRUE(done->pop({}, 10s).has_value());
    ASSERT_TRUE(waitFor([&] {
        const std::scoped_lock lock(log->mutex);
        return log->closed;
    }));
    host.stop();
    const std::scoped_lock lock(record->mutex);
    EXPECT_EQ(record->connects - record->disconnects, 0); // nothing left connected
}

// Review B5: a loss reported from inside IBleApi::connect() (a synchronous controller error) is
// delivered to the link with its own error, not lost until the connect deadline.
TEST(BleHost, LossInsideConnectReachesTheLink) {
    auto record = std::make_shared<ApiRecord>();
    BleHost host([record]() -> std::unique_ptr<hio::ble::IBleApi> {
        auto api = std::make_unique<RecordedApi>(record);
        auto listener = std::make_shared<hio::ble::IBleListener*>(nullptr);
        ON_CALL(*api, setListener(_)).WillByDefault([listener](hio::ble::IBleListener* value) {
            *listener = value;
        });
        ON_CALL(*api, connect(_, _)).WillByDefault([listener](const auto&, const auto&) {
            if (*listener != nullptr) {
                (*listener)->onLinkLost(5, hio::makeError(ErrorCode::BleNotPaired, "sync"));
            }
            return hio::Result<LinkId>(5);
        });
        return api;
    });
    auto log = std::make_shared<SyncLog>();
    ASSERT_TRUE(host.post(hio::ble::OpenLink{.client = host.newClientId(),
                                             .device = "F1:22:33:44:55:66",
                                             .uuids = {},
                                             .handler = std::make_unique<SyncHandler>(log),
                                             .timing = {}}));
    ASSERT_TRUE(waitFor(
        [&] {
            const std::scoped_lock lock(log->mutex);
            return log->events.size() >= 2;
        },
        2s)); // far less than the 20 s connect deadline
    const std::scoped_lock lock(log->mutex);
    EXPECT_EQ(log->events, (std::vector<std::string>{"connecting", "down ble.not_paired"}));
}

TEST(BleHost, CloseLinkAcknowledges) {
    auto record = std::make_shared<ApiRecord>();
    BleHost host(recordedFactory(record));
    auto log = std::make_shared<SyncLog>();
    const auto client = host.newClientId();
    ASSERT_TRUE(host.post(hio::ble::OpenLink{.client = client,
                                             .device = "F1:22:33:44:55:66",
                                             .uuids = {},
                                             .handler = std::make_unique<SyncHandler>(log),
                                             .timing = {}}));
    auto done = std::make_shared<hio::BoundedQueue<bool>>(1, hio::OverflowPolicy::Reject);
    ASSERT_TRUE(host.post(hio::ble::CloseLink{.client = client, .done = done}));
    ASSERT_TRUE(done->pop({}, 5s).has_value());
    const std::scoped_lock lock(log->mutex);
    EXPECT_TRUE(log->closed);
    EXPECT_EQ(log->events.front(), "connecting");
}

TEST(BleHost, AdapterOffFailsTheLink) {
    auto record = std::make_shared<ApiRecord>();
    BleHost host([record]() -> std::unique_ptr<hio::ble::IBleApi> {
        auto api = std::make_unique<RecordedApi>(record);
        ON_CALL(*api, adapter())
            .WillByDefault(Return(hio::ble::AdapterInfo{
                .state = hio::ble::AdapterState::PoweredOff, .name = "hci0", .address = ""}));
        return api;
    });
    auto log = std::make_shared<SyncLog>();
    ASSERT_TRUE(host.post(hio::ble::OpenLink{.client = 1,
                                             .device = "F1:22:33:44:55:66",
                                             .uuids = {},
                                             .handler = std::make_unique<SyncHandler>(log),
                                             .timing = {}}));
    ASSERT_TRUE(waitFor([&] {
        const std::scoped_lock lock(log->mutex);
        return !log->events.empty();
    }));
    const std::scoped_lock lock(log->mutex);
    EXPECT_EQ(log->events, std::vector<std::string>{"down ble.adapter_off"});
}

TEST(BleHost, ConcurrentPostsAndStop) {
    auto record = std::make_shared<ApiRecord>();
    auto host = std::make_unique<BleHost>(recordedFactory(record));
    std::atomic<bool> go{false};
    std::vector<std::jthread> threads;
    threads.reserve(4);
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&host, &go, t] {
            while (!go.load()) {
                std::this_thread::yield();
            }
            for (int i = 0; i < 200; ++i) {
                [[maybe_unused]] const hio::Status posted = host->post(hio::ble::SendLine{
                    .client = static_cast<hio::ble::ClientId>(t), .line = std::to_string(i)});
                [[maybe_unused]] const hio::ble::ClientId id = host->newClientId();
            }
        });
    }
    go.store(true);
    std::this_thread::sleep_for(1ms);
    host->stop();
    threads.clear(); // join
    host.reset();
    SUCCEED();
}

TEST(BleHost, ScanIsSharedAndStopsWhenNobodyScans) {
    auto record = std::make_shared<ApiRecord>();
    std::atomic<int> starts{0};
    std::atomic<int> stops{0};
    BleHost host([&]() -> std::unique_ptr<hio::ble::IBleApi> {
        auto api = std::make_unique<RecordedApi>(record);
        ON_CALL(*api, startScan(_)).WillByDefault([&](std::chrono::milliseconds) {
            ++starts;
            return hio::Status{};
        });
        ON_CALL(*api, stopScan()).WillByDefault([&] { ++stops; });
        return api;
    });
    auto first = BleHost::subscribe([] {});
    auto second = BleHost::subscribe([] {});
    ASSERT_TRUE(host.post(hio::ble::StartScan{.subscriber = first, .duration = 50ms}));
    ASSERT_TRUE(host.post(hio::ble::StartScan{.subscriber = second, .duration = 0ms}));
    ASSERT_TRUE(waitFor([&] { return first->size() > 0; }));
    const auto finished = first->pop();
    ASSERT_TRUE(finished.has_value());
    EXPECT_TRUE(
        std::holds_alternative<hio::ble::ScanFinished>(finished.value_or(hio::ble::BleEvent{})));
    EXPECT_EQ(stops.load(), 0); // the second still scans
    ASSERT_TRUE(host.post(hio::ble::StopScan{.subscriber = second}));
    ASSERT_TRUE(waitFor([&] { return stops.load() == 1; }));
    EXPECT_EQ(starts.load(), 1);
    EXPECT_EQ(second->size(), 0U); // an unbounded scan ends without a result
    first->detach();
    second->detach();
}

TEST(BleHost, PairingAlreadyPairedAndNotSupported) {
    auto record = std::make_shared<ApiRecord>();
    BleHost host([record]() -> std::unique_ptr<hio::ble::IBleApi> {
        auto api = std::make_unique<RecordedApi>(record);
        ON_CALL(*api, isPaired(hio::ble::BleDeviceId("A"))).WillByDefault(Return(true));
        ON_CALL(*api, requestPairing(hio::ble::BleDeviceId("B")))
            .WillByDefault(Return(hio::Status(hio::fail(ErrorCode::BlePairingNotSupported))));
        return api;
    });
    auto subscriber = BleHost::subscribe([] {});
    ASSERT_TRUE(host.post(
        hio::ble::PairDevice{.device = "A", .subscriber = subscriber, .client = std::nullopt}));
    ASSERT_TRUE(host.post(
        hio::ble::PairDevice{.device = "B", .subscriber = subscriber, .client = std::nullopt}));
    ASSERT_TRUE(waitFor([&] { return subscriber->size() >= 2; }));
    const auto a =
        std::get<hio::ble::PairingResult>(subscriber->pop().value_or(hio::ble::BleEvent{}));
    EXPECT_EQ(a.device, "A");
    EXPECT_TRUE(a.status.has_value());
    EXPECT_TRUE(a.alreadyPaired);
    const auto b =
        std::get<hio::ble::PairingResult>(subscriber->pop().value_or(hio::ble::BleEvent{}));
    ASSERT_FALSE(b.status.has_value());
    EXPECT_EQ(b.status.error().code, ErrorCode::BlePairingNotSupported);
    subscriber->detach();
}

TEST(EventChannel, DropsOnlyDroppableEventsWhenFull) {
    int wakes = 0;
    hio::ble::EventChannel<int> channel(
        3, [](const int& value) { return value >= 0; }, [&wakes] { ++wakes; });
    EXPECT_TRUE(channel.push(-1)); // never dropped
    EXPECT_TRUE(channel.push(1));
    EXPECT_TRUE(channel.push(2));
    EXPECT_TRUE(channel.push(3)); // drops 1
    EXPECT_EQ(channel.droppedCount(), 1U);
    EXPECT_EQ(wakes, 4);
    EXPECT_EQ(channel.pop(), std::optional<int>(-1));
    EXPECT_EQ(channel.pop(), std::optional<int>(2));
    channel.detach();
    EXPECT_FALSE(channel.push(4));
    EXPECT_EQ(wakes, 4);                             // no wake-up after detach()
    EXPECT_EQ(channel.pop(), std::optional<int>(3)); // queued before detach
}

} // namespace
