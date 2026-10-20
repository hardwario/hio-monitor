#pragma once

// BleWorker - the BLE thread's side of BleHost (ADR-0080, ADR-0129). Lives on the BLE thread only:
// created there (with the IBleApi), drained there, destroyed there.

#include "hio/core/bounded_queue.h"
#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/hw/ble/ble_api.h"
#include "hio/hw/ble/ble_host.h"
#include "hio/hw/ble/nus_link.h"
#include "hio/hw/ble/types.h"

#include <QEventLoop>
#include <QObject>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <vector>

namespace hio::ble {

class BleWorker;

namespace detail {

// State shared by the host (any thread) and the worker (BLE thread).
struct HostShared {
    BoundedQueue<BleCommand> commands{BleHost::commandQueueCapacity, OverflowPolicy::Reject};
    // Review B3: link closes have reserved capacity. Every OpenLink reserves one close slot for
    // its client (at most BleHost::maxLinks, guarded by reserveMutex); the client's CloseLink goes
    // into `closes`, which therefore always has room, and the worker releases the slot when it
    // processed the close.
    BoundedQueue<CloseLink> closes{BleHost::maxLinks, OverflowPolicy::Reject};
    std::mutex reserveMutex;
    std::set<ClientId> reserved; // guarded by reserveMutex; <= BleHost::maxLinks

    [[nodiscard]] bool reserveClose(ClientId client) {
        const std::scoped_lock lock(reserveMutex);
        if (reserved.size() >= BleHost::maxLinks && !reserved.contains(client)) {
            return false;
        }
        reserved.insert(client);
        return true;
    }
    [[nodiscard]] bool isReserved(ClientId client) {
        const std::scoped_lock lock(reserveMutex);
        return reserved.contains(client);
    }
    void releaseClose(ClientId client) {
        const std::scoped_lock lock(reserveMutex);
        reserved.erase(client);
    }
    std::atomic<bool> drainPending{false};
    std::atomic<bool> shutdownRequested{false};
    // Set as the last action of the BLE thread's function; stop() waits for it (at most
    // BleHost::stopWarningAfter before the warning of ADR-0130), then joins the thread.
    std::atomic<bool> threadFinished{false};
    std::mutex finishMutex;
    std::condition_variable finishedCondition;

    void markFinished() {
        {
            const std::scoped_lock lock(finishMutex);
            threadFinished.store(true, std::memory_order_release);
        }
        finishedCondition.notify_all();
    }
    [[nodiscard]] bool waitFinished(std::chrono::milliseconds timeout) {
        std::unique_lock lock(finishMutex);
        return finishedCondition.wait_for(lock, timeout, [this] { return threadFinished.load(); });
    }
    std::mutex workerMutex;
    BleWorker* worker = nullptr; // guarded by workerMutex; set while the worker exists

    // Queues one drain call on the worker (coalesced). Any thread.
    void wake();
};

} // namespace detail

class BleWorker final : public QObject, public IBleListener {
    Q_OBJECT

public:
    BleWorker(std::unique_ptr<IBleApi> api, detail::HostShared& shared, QEventLoop& loop);
    ~BleWorker() override;
    BleWorker(const BleWorker&) = delete;
    BleWorker& operator=(const BleWorker&) = delete;
    BleWorker(BleWorker&&) = delete;
    BleWorker& operator=(BleWorker&&) = delete;

    // Invoked (queued, by name) by HostShared::wake().
    Q_INVOKABLE void drainCommands();

    // IBleListener
    void onAdvertisement(const Advertisement& advertisement) override;
    void onScanFinished(std::optional<hio::Error> error) override;
    void onLinkConnecting(LinkId link, bool waitingForPairing) override;
    void onLinkReady(LinkId link, LinkReady ready) override;
    void onLinkData(LinkId link, std::span<const std::byte> data) override;
    void onWriteDone(LinkId link, std::uint64_t token, std::optional<hio::Error> error) override;
    void onLinkLost(LinkId link, hio::Error error) override;
    void onPairingFinished(BleDeviceId device, Status status) override;

private:
    struct Scanner {
        std::shared_ptr<Subscription> subscriber;
        std::optional<std::chrono::steady_clock::time_point> until; // nullopt = until StopScan
    };
    struct PairingRequest {
        BleDeviceId device;
        std::shared_ptr<Subscription> subscriber;
        std::optional<ClientId> client;
    };

    void handle(BleCommand command);
    void startScan(StartScan command);
    void stopScan(const std::shared_ptr<Subscription>& subscriber);
    void updateScan();
    void finishScanners(const std::optional<Error>& error, bool all);
    void openLink(OpenLink command);
    void closeLink(const CloseLink& command);
    void drainCloses();
    static void acknowledge(const std::shared_ptr<BoundedQueue<bool>>& done);
    void connectLink(ClientId client, bool automatic);
    void pair(PairDevice command);
    void deliverPairing(const PairingRequest& request, const Status& status, bool alreadyPaired);
    void shutdown();
    [[nodiscard]] NusLink* linkOf(LinkId link);

    std::unique_ptr<IBleApi> m_api;
    detail::HostShared* m_shared;    // non-owning; outlives the worker
    QEventLoop* m_loop;              // non-owning; the thread's loop
    std::vector<Scanner> m_scanners; // bounded by the subscribers (scanners, sessions)
    bool m_scanning = false;
    QTimer m_scanTimer;                                   // the earliest scanner deadline
    std::map<ClientId, std::unique_ptr<NusLink>> m_links; // <= BleHost::maxLinks
    std::vector<PairingRequest> m_pairings;               // bounded by the links + subscribers
    // Clients whose CloseLink was processed before their OpenLink (B3); <= BleHost::maxLinks
    // (only reserved clients).
    // The close's acknowledgement is sent when the link was closed.
    std::map<ClientId, std::shared_ptr<BoundedQueue<bool>>> m_closedEarly;
    // Review B5: onLinkLost() for a LinkId no link owns yet (reported from inside
    // IBleApi::connect()); delivered when connect() returned that id. Bounded: maxOrphanLost.
    std::map<LinkId, Error> m_orphanLost;
    bool m_shutDown = false;
};

} // namespace hio::ble
