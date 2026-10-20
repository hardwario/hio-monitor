#pragma once

// BleHost - the one application-owned BLE thread with an event loop (ADR-0080, ADR-0129,
// ADR-0130, ADR-0141; plan/features/0011-m5-ble-backend-cli.md).
//
// The host owns the BLE thread (started on first use) running a QEventLoop and a BleWorker.
// ADR-0141: the thread is an owned std::jthread (Qt adopts it; QEventLoop, QTimer and
// QtBluetooth work there), not a QThread. The BleWorker owns the IBleApi (QtBleApi: all QtBluetooth
// objects are created, used and destroyed on that thread) and the NusLinks. Owners talk to it with
// commands (post(), any thread) through a BoundedQueue (commandQueueCapacity, Reject ->
// internal.queue_full), drained by one coalesced queued call. Results come back:
//  - to a Subscription (EventChannel<BleEvent>, eventQueueCapacity, only advertisements are
//    dropped when full): scan advertisements and results, pairing results;
//  - to a link's ILinkHandler, which the owner passed with OpenLink and which runs on the BLE
//    thread.
// Scans are shared: the stack scans while at least one subscriber asked for it (StartScan with a
// duration, or 0 = until StopScan); every scanning subscriber receives all advertisements.
//
// stop() (also the destructor) closes every link (handlers get onClosed()), stops the scan,
// deletes the IBleApi on the BLE thread, quits the loop and waits for the thread; after
// stopWarningAfter it logs a warning and keeps waiting (ADR-0130). Commands posted before stop()
// are processed first.
//
// Thread safety: post(), newClientId(), subscribe() and queryAdapter() from any thread (the latter
// never on the BLE thread); stop() and the destructor on the owner thread.

#include "hio/core/bounded_queue.h"
#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/hw/ble/ble_api.h"
#include "hio/hw/ble/event_channel.h"
#include "hio/hw/ble/link_handler.h"
#include "hio/hw/ble/nus_link.h"
#include "hio/hw/ble/types.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>

namespace hio::ble {

// ---- events to subscribers --------------------------------------------------------------------

struct ScanFinished {
    std::optional<Error> error;
};

struct PairingResult {
    BleDeviceId device;
    Status status;
    bool alreadyPaired = false; // the system had a bond already (IBleApi::isPaired)
};

using BleEvent = std::variant<Advertisement, ScanFinished, PairingResult>;

// Advertisements may be dropped when a subscriber's queue is full; results never.
[[nodiscard]] inline bool isDroppableBleEvent(const BleEvent& event) {
    return std::holds_alternative<Advertisement>(event);
}

using Subscription = EventChannel<BleEvent>;

// ---- commands ---------------------------------------------------------------------------------

using ClientId = std::uint64_t; // a link owner's handle (BleHost::newClientId())

struct StartScan {
    std::shared_ptr<Subscription> subscriber;
    std::chrono::milliseconds duration{0}; // 0 = until StopScan
};
struct StopScan {
    std::shared_ptr<Subscription> subscriber;
};
// Registers a link with its handler and connects it.
struct OpenLink {
    ClientId client = 0;
    BleDeviceId device;
    NusUuids uuids;
    std::unique_ptr<ILinkHandler> handler;
    LinkTiming timing;
};
// Connects the link again (an existing connection is dropped first).
struct ConnectLink {
    ClientId client = 0;
    bool automatic = false;
};
struct SendLine {
    ClientId client = 0;
    std::string line;
};
// Closes the link; `done` (optional) receives one element when the handler was closed. Uses the
// space its OpenLink reserved (never internal.queue_full); the worker handles closes after the
// commands it drained, and a close that overtakes its OpenLink still closes the link (B3).
struct CloseLink {
    ClientId client = 0;
    std::shared_ptr<BoundedQueue<bool>> done;
};
// Pairs `device` (ADR-0084); the result goes to the subscriber and/or the client's handler.
struct PairDevice {
    BleDeviceId device;
    std::shared_ptr<Subscription> subscriber;
    std::optional<ClientId> client;
};
struct QueryAdapter {
    std::shared_ptr<BoundedQueue<AdapterInfo>> reply;
};

using BleCommand = std::variant<StartScan, StopScan, OpenLink, ConnectLink, SendLine, CloseLink,
                                PairDevice, QueryAdapter>;

namespace detail {
struct HostShared;
} // namespace detail

class BleHost {
public:
    using ApiFactory = std::function<std::unique_ptr<IBleApi>()>; // called on the BLE thread

    static constexpr std::size_t commandQueueCapacity = 256;
    static constexpr std::size_t eventQueueCapacity = 10000;
    static constexpr std::size_t maxLinks = 8;
    static constexpr std::chrono::milliseconds stopWarningAfter{3000};

    explicit BleHost(ApiFactory factory = createBleApi);
    ~BleHost(); // stop()
    BleHost(const BleHost&) = delete;
    BleHost& operator=(const BleHost&) = delete;
    BleHost(BleHost&&) = delete;
    BleHost& operator=(BleHost&&) = delete;

    // Queues a command (starts the thread on first use). Errors: internal.queue_full, cancelled
    // (stopped); OpenLink also internal.session_limit (maxLinks links open or closing). Review
    // B3: an OpenLink reserves room for its client's CloseLink, which then never fails for lack
    // of space (closes bypass the command queue; one per opened link, at most maxLinks).
    [[nodiscard]] Status post(BleCommand command);
    [[nodiscard]] ClientId newClientId() noexcept;
    // A new subscription; `wake` is called on the BLE thread after events were queued (keep it
    // cheap). The subscriber calls detach() before it goes away.
    [[nodiscard]] static std::shared_ptr<Subscription> subscribe(std::function<void()> wake);
    // The adapter and permission state (IBleApi::adapter(), never scans). Blocks up to `timeout`.
    // Errors: timeout, cancelled (stopped), internal.queue_full, internal.unexpected (called on
    // the BLE thread).
    [[nodiscard]] Result<AdapterInfo> queryAdapter(std::chrono::milliseconds timeout);

    void stop();
    [[nodiscard]] bool isBleThread() const;

private:
    void ensureStarted();
    void runThread();
    void runWorker();

    ApiFactory m_factory;
    std::shared_ptr<detail::HostShared> m_shared;
    mutable std::mutex m_startMutex; // guards m_thread creation / m_stopped
    std::jthread m_thread;           // guarded by m_startMutex
    std::thread::id m_threadId;      // guarded by m_startMutex
    bool m_stopped = false;          // guarded by m_startMutex
    std::atomic<ClientId> m_nextClient{1};
};

} // namespace hio::ble
