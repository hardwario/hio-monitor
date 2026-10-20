#include "ble_worker.h"

#include "hio/core/bounded_queue.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/hw/ble/ble_api.h"
#include "hio/hw/ble/ble_host.h"
#include "hio/hw/ble/nus_link.h"
#include "hio/hw/ble/types.h"

#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QObject>
#include <QTimer>
#include <QtCore/qnamespace.h>
#include <QtLogging>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace hio::ble {

namespace {

template <class... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
};

using Clock = std::chrono::steady_clock;

// Pairing requests waiting for a result (bounded; more -> internal.queue_full).
constexpr std::size_t maxPendingPairings = 16;

// Losses of unknown links kept until the running connect() returns (B5).
constexpr std::size_t maxOrphanLost = 16;

} // namespace

namespace detail {

void HostShared::wake() {
    const std::scoped_lock lock(workerMutex);
    if (worker != nullptr && !drainPending.exchange(true)) {
        // By name: the queued call object is created and consumed inside Qt (see
        // DeviceSession::wakeUp and tools/sanitizers/tsan.supp).
        QMetaObject::invokeMethod(worker, "drainCommands", Qt::QueuedConnection);
    }
}

} // namespace detail

BleWorker::BleWorker(std::unique_ptr<IBleApi> api, detail::HostShared& shared, QEventLoop& loop)
    : m_api(std::move(api)), m_shared(&shared), m_loop(&loop) {
    m_api->setListener(this);
    m_scanTimer.setSingleShot(true);
    QObject::connect(&m_scanTimer, &QTimer::timeout, this, [this] { updateScan(); });
}

BleWorker::~BleWorker() { shutdown(); }

void BleWorker::drainCommands() {
    m_shared->drainPending.store(false);
    // Bounded per call; anything left is drained by the next (rescheduled) call. Closes go first,
    // before every command (review N1): a close posted before a new OpenLink of the same device
    // is always processed before it, so the old link and console file are closed before the new
    // link connects and opens its file. A close that overtakes its own OpenLink is remembered
    // (m_closedEarly).
    for (std::size_t i = 0; i < BleHost::commandQueueCapacity && !m_shutDown; ++i) {
        drainCloses();
        if (m_shutDown) {
            break;
        }
        auto command = m_shared->commands.pop({}, std::chrono::milliseconds(0));
        if (!command) {
            break;
        }
        handle(std::move(*command));
    }
    if (!m_shutDown) {
        drainCloses();
    }
    if (m_shutDown) {
        return;
    }
    if (m_shared->commands.size() > 0 || m_shared->closes.size() > 0) {
        m_shared->wake();
        return;
    }
    if (m_shared->shutdownRequested.load()) {
        shutdown();
        m_loop->quit();
    }
}

void BleWorker::handle(BleCommand command) {
    std::visit(Overloaded{
                   [this](StartScan& item) { startScan(std::move(item)); },
                   [this](const StopScan& item) { stopScan(item.subscriber); },
                   [this](OpenLink& item) { openLink(std::move(item)); },
                   [this](const ConnectLink& item) { connectLink(item.client, item.automatic); },
                   [this](SendLine& item) {
                       if (NusLink* link = m_links.contains(item.client)
                                               ? m_links.at(item.client).get()
                                               : nullptr) {
                           link->sendLine(std::move(item.line));
                       }
                   },
                   [this](const CloseLink& item) { closeLink(item); },
                   [this](PairDevice& item) { pair(std::move(item)); },
                   [this](StopLink& item) {
                       if (const auto it = m_links.find(item.client); it != m_links.end()) {
                           it->second->stop(std::move(item.error));
                       }
                   },
                   [this](const QueryAdapter& item) {
                       if (item.reply && !item.reply->push(m_api->adapter())) {
                           qCDebug(log::lcBle) << "Adapter state not delivered (no waiter)";
                       }
                   },
               },
               command);
}

// ---- scanning ----------------------------------------------------------------------------------

void BleWorker::startScan(StartScan command) {
    if (!command.subscriber) {
        return;
    }
    std::optional<Clock::time_point> until;
    if (command.duration.count() > 0) {
        until = Clock::now() + command.duration;
    }
    const auto it = std::ranges::find(m_scanners, command.subscriber, &Scanner::subscriber);
    if (it != m_scanners.end()) {
        it->until = until; // restarted
    } else {
        m_scanners.push_back(Scanner{.subscriber = std::move(command.subscriber), .until = until});
    }
    if (!m_scanning) {
        const AdapterInfo adapter = m_api->adapter();
        if (auto problem = adapterError(adapter)) {
            finishScanners(problem, true);
            return;
        }
        if (Status started = m_api->startScan(std::chrono::milliseconds(0)); !started) {
            finishScanners(started.error(), true);
            return;
        }
        m_scanning = true;
        qCDebug(log::lcBle) << "Scan started";
    }
    updateScan();
}

void BleWorker::stopScan(const std::shared_ptr<Subscription>& subscriber) {
    std::erase_if(m_scanners, [&subscriber](const Scanner& scanner) {
        return scanner.subscriber == subscriber;
    });
    updateScan();
}

// Ends the scanners whose time is up, stops the stack's scan when nobody scans any more and arms
// the timer for the next deadline.
void BleWorker::updateScan() {
    const auto now = Clock::now();
    std::optional<Clock::time_point> next;
    for (auto it = m_scanners.begin(); it != m_scanners.end();) {
        if (it->until && *it->until <= now) {
            static_cast<void>(it->subscriber->push(ScanFinished{.error = std::nullopt}));
            it = m_scanners.erase(it);
            continue;
        }
        if (it->until && (!next || *it->until < *next)) {
            next = it->until;
        }
        ++it;
    }
    if (m_scanners.empty() && m_scanning) {
        m_api->stopScan();
        m_scanning = false;
        qCDebug(log::lcBle) << "Scan stopped";
    }
    m_scanTimer.stop();
    if (next) {
        m_scanTimer.start(std::chrono::duration_cast<std::chrono::milliseconds>(*next - now) +
                          std::chrono::milliseconds(1));
    }
}

void BleWorker::finishScanners(const std::optional<Error>& error, bool all) {
    for (const Scanner& scanner : m_scanners) {
        if (all || scanner.until) {
            static_cast<void>(scanner.subscriber->push(ScanFinished{.error = error}));
        }
    }
    m_scanners.clear();
    m_scanTimer.stop();
}

void BleWorker::onAdvertisement(const Advertisement& advertisement) {
    // A subscriber that detached is dropped (its push fails).
    std::erase_if(m_scanners, [&advertisement](const Scanner& scanner) {
        return !scanner.subscriber->push(advertisement);
    });
    if (m_scanners.empty() && m_scanning) {
        updateScan();
    }
}

void BleWorker::onScanFinished(std::optional<hio::Error> error) {
    if (!m_scanning) {
        return;
    }
    m_scanning = false;
    if (error) {
        qCWarning(log::lcBle).noquote()
            << "Scan failed:" << toQString(error->message) << toQString(error->detail);
    }
    finishScanners(error, true);
}

// ---- links
// ---------------------------------------------------------------------------------------

NusLink* BleWorker::linkOf(LinkId link) {
    for (auto& [client, item] : m_links) {
        if (item->link() == link) {
            return item.get();
        }
    }
    return nullptr;
}

void BleWorker::drainCloses() {
    // At most BleHost::maxLinks (the queue's capacity).
    for (std::size_t i = 0; i < BleHost::maxLinks; ++i) {
        auto close = m_shared->closes.pop({}, std::chrono::milliseconds(0));
        if (!close) {
            break;
        }
        closeLink(*close);
    }
}

void BleWorker::closeLink(const CloseLink& command) {
    std::erase_if(m_pairings, [&command](const PairingRequest& request) {
        return request.client == command.client && !request.subscriber;
    });
    if (m_links.erase(command.client) == 0 && m_shared->isReserved(command.client)) {
        // The close overtook its OpenLink (it uses the reserved close path): the link is closed,
        // and the close acknowledged, when the OpenLink arrives (B3, N1).
        m_closedEarly.insert_or_assign(command.client, command.done);
        m_shared->releaseClose(command.client);
        return;
    }
    m_shared->releaseClose(command.client);
    acknowledge(command.done);
}

void BleWorker::acknowledge(const std::shared_ptr<BoundedQueue<bool>>& done) {
    if (done && !done->push(true)) {
        qCDebug(log::lcBle) << "Link closed after the owner stopped waiting";
    }
}

void BleWorker::openLink(OpenLink command) {
    if (!command.handler) {
        return;
    }
    if (auto early = m_closedEarly.extract(command.client)) {
        command.handler->onClosed(); // closed before it was opened (B3)
        acknowledge(early.mapped());
        return;
    }
    if (m_links.contains(command.client) || m_links.size() >= BleHost::maxLinks) {
        Error error = makeError(ErrorCode::InternalSessionLimit,
                                std::format("{} Bluetooth links are open", m_links.size()));
        command.handler->onDown(std::move(error));
        command.handler->onClosed();
        return;
    }
    auto link =
        std::make_unique<NusLink>(*m_api, std::move(command.device), std::move(command.uuids),
                                  std::move(command.handler), command.timing);
    m_links.emplace(command.client, std::move(link));
    connectLink(command.client, false);
}

void BleWorker::connectLink(ClientId client, bool automatic) {
    const auto it = m_links.find(client);
    if (it == m_links.end()) {
        return;
    }
    if (auto problem = adapterError(m_api->adapter())) {
        it->second->fail(std::move(*problem));
        return;
    }
    it->second->connect(automatic);
    // Review B5: a loss reported from inside IBleApi::connect() for the id it then returned.
    if (const auto link = it->second->link()) {
        if (auto lost = m_orphanLost.extract(*link)) {
            it->second->onLinkLost(std::move(lost.mapped()));
        }
    }
    m_orphanLost.clear(); // only a loss inside this connect() call can belong to a new link
}

void BleWorker::onLinkConnecting(LinkId link, bool waitingForPairing) {
    if (NusLink* item = linkOf(link)) {
        item->onLinkConnecting(waitingForPairing);
    }
}

void BleWorker::onLinkReady(LinkId link, LinkReady ready) {
    if (NusLink* item = linkOf(link)) {
        item->onLinkReady(ready);
    }
}

void BleWorker::onLinkData(LinkId link, std::span<const std::byte> data) {
    if (NusLink* item = linkOf(link)) {
        item->onLinkData(data);
    }
}

void BleWorker::onWriteDone(LinkId link, std::uint64_t token, std::optional<hio::Error> error) {
    if (NusLink* item = linkOf(link)) {
        item->onWriteDone(token, std::move(error));
    }
}

void BleWorker::onLinkLost(LinkId link, hio::Error error) {
    if (NusLink* item = linkOf(link)) {
        item->onLinkLost(std::move(error));
        return;
    }
    if (m_orphanLost.size() < maxOrphanLost) {
        m_orphanLost.insert_or_assign(link, std::move(error)); // B5: maybe inside connect()
    }
}

// ---- pairing
// -------------------------------------------------------------------------------------

void BleWorker::deliverPairing(const PairingRequest& request, const Status& status,
                               bool alreadyPaired) {
    if (request.subscriber) {
        static_cast<void>(request.subscriber->push(PairingResult{
            .device = request.device, .status = status, .alreadyPaired = alreadyPaired}));
    }
    if (request.client) {
        if (const auto it = m_links.find(*request.client); it != m_links.end()) {
            it->second->pairingFinished(status, alreadyPaired);
        }
    }
}

void BleWorker::pair(PairDevice command) {
    PairingRequest request{.device = std::move(command.device),
                           .subscriber = std::move(command.subscriber),
                           .client = command.client};
    if (auto problem = adapterError(m_api->adapter())) {
        deliverPairing(request, std::unexpected(std::move(*problem)), false);
        return;
    }
    if (m_api->isPaired(request.device)) {
        deliverPairing(request, Status{}, true);
        return;
    }
    if (m_pairings.size() >= maxPendingPairings) {
        deliverPairing(
            request,
            std::unexpected(makeError(ErrorCode::InternalQueueFull, "too many pairing requests")),
            false);
        return;
    }
    const bool running = std::ranges::any_of(m_pairings, [&request](const PairingRequest& other) {
        return sameDeviceId(other.device, request.device);
    });
    if (!running) {
        if (Status started = m_api->requestPairing(request.device); !started) {
            deliverPairing(request, started, false);
            return;
        }
        qCInfo(log::lcBle) << "Pairing requested:" << toQString(request.device);
    }
    m_pairings.push_back(std::move(request));
}

void BleWorker::onPairingFinished(BleDeviceId device, Status status) {
    qCInfo(log::lcBle).noquote() << "Pairing finished:" << toQString(device)
                                 << (status ? QStringLiteral("ok")
                                            : toQString(status.error().detail));
    std::vector<PairingRequest> finished;
    std::erase_if(m_pairings, [&](PairingRequest& request) {
        if (!sameDeviceId(request.device, device)) { // review B6
            return false;
        }
        finished.push_back(std::move(request));
        return true;
    });
    for (const PairingRequest& request : finished) {
        deliverPairing(request, status, false);
    }
}

// ---- shutdown
// ------------------------------------------------------------------------------------

void BleWorker::shutdown() {
    if (m_shutDown) {
        return;
    }
    m_shutDown = true;
    m_scanTimer.stop();
    m_links.clear(); // each link disconnects and closes its handler (console file flushed)
    while (auto close = m_shared->closes.pop({}, std::chrono::milliseconds(0))) {
        acknowledge(close->done);
    }
    for (auto& [client, done] : m_closedEarly) {
        acknowledge(done);
    }
    m_closedEarly.clear();
    if (m_scanning) {
        m_api->stopScan();
        m_scanning = false;
    }
    m_scanners.clear();
    m_pairings.clear();
    m_api->setListener(nullptr);
    m_api.reset(); // the platform objects are deleted here, on the BLE thread
    // Objects the API released with deleteLater() go now, still on this thread.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    qCDebug(log::lcBle) << "BLE worker stopped";
}

} // namespace hio::ble
