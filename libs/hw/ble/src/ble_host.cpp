#include "hio/hw/ble/ble_host.h"

#include "ble_worker.h"
#include "hio/core/bounded_queue.h"
#include "hio/core/error.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/hw/ble/ble_api.h"
#include "hio/hw/ble/types.h"

#include <QEventLoop>
#include <QLoggingCategory>
#include <QString>
#include <QtLogging>

#include <atomic>
#include <chrono>
#include <exception>
#include <expected>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <variant>

namespace hio::ble {

namespace {

// Publishes the worker in HostShared::worker (for wake()) while it exists (RAII, review B9).
class WorkerRegistration {
public:
    WorkerRegistration(detail::HostShared& shared, BleWorker& worker) : m_shared(&shared) {
        const std::scoped_lock lock(m_shared->workerMutex);
        m_shared->worker = &worker;
    }
    ~WorkerRegistration() {
        const std::scoped_lock lock(m_shared->workerMutex);
        m_shared->worker = nullptr;
    }
    WorkerRegistration(const WorkerRegistration&) = delete;
    WorkerRegistration& operator=(const WorkerRegistration&) = delete;
    WorkerRegistration(WorkerRegistration&&) = delete;
    WorkerRegistration& operator=(WorkerRegistration&&) = delete;

private:
    detail::HostShared* m_shared; // non-owning; outlives the registration
};

} // namespace

BleHost::BleHost(ApiFactory factory)
    : m_factory(std::move(factory)), m_shared(std::make_shared<detail::HostShared>()) {}

BleHost::~BleHost() { stop(); }

ClientId BleHost::newClientId() noexcept { return m_nextClient.fetch_add(1); }

std::shared_ptr<Subscription> BleHost::subscribe(std::function<void()> wake) {
    return std::make_shared<Subscription>(eventQueueCapacity, &isDroppableBleEvent,
                                          std::move(wake));
}

void BleHost::ensureStarted() {
    const std::scoped_lock lock(m_startMutex);
    if (m_thread.joinable() || m_stopped) {
        return;
    }
    // ADR-0141: an owned std::jthread running a QEventLoop (Qt adopts the thread).
    m_thread = std::jthread([this] { runThread(); });
    m_threadId = m_thread.get_id();
    qCDebug(log::lcBle) << "BLE thread started";
}

void BleHost::runThread() {
    runWorker();
    m_shared->markFinished();
}

void BleHost::runWorker() {
    // No exception leaves the thread (ADR-0007); a failure stops the host's processing.
    try {
        QEventLoop loop;
        std::unique_ptr<IBleApi> api = m_factory();
        if (!api) {
            qCCritical(log::lcBle) << "No Bluetooth backend";
            m_shared->commands.close();
            return;
        }
        BleWorker worker(std::move(api), *m_shared, loop);
        // Cleared again on every way out of this scope, also by an exception (review B9).
        const WorkerRegistration registration(*m_shared, worker);
        m_shared->wake(); // commands posted before the worker existed
        loop.exec();
    } catch (const std::exception& error) {
        qCCritical(log::lcBle) << "BLE thread failed:" << error.what();
        m_shared->commands.close();
    } catch (...) {
        qCCritical(log::lcBle) << "BLE thread failed";
        m_shared->commands.close();
    }
}

Status BleHost::post(BleCommand command) {
    ensureStarted();
    // Review B3: a close never competes with other commands for space.
    if (auto* close = std::get_if<CloseLink>(&command)) {
        if (Status pushed = m_shared->closes.push(std::move(*close)); !pushed) {
            return pushed; // only after stop() (closed) or a close without its OpenLink
        }
        m_shared->wake();
        return {};
    }
    std::optional<ClientId> reserved;
    if (const auto* open = std::get_if<OpenLink>(&command)) {
        if (!m_shared->reserveClose(open->client)) {
            return std::unexpected(
                makeError(ErrorCode::InternalSessionLimit,
                          std::format("{} Bluetooth links are open or closing", maxLinks)));
        }
        reserved = open->client;
    }
    if (Status pushed = m_shared->commands.push(std::move(command)); !pushed) {
        if (reserved) {
            m_shared->releaseClose(*reserved);
        }
        return pushed;
    }
    m_shared->wake();
    return {};
}

Result<AdapterInfo> BleHost::queryAdapter(std::chrono::milliseconds timeout) {
    if (isBleThread()) {
        return std::unexpected(
            makeError(ErrorCode::InternalUnexpected, "queryAdapter() called on the BLE thread"));
    }
    auto reply = std::make_shared<BoundedQueue<AdapterInfo>>(1, OverflowPolicy::Reject);
    if (Status posted = post(QueryAdapter{.reply = reply}); !posted) {
        return std::unexpected(std::move(posted).error());
    }
    auto info = reply->pop({}, timeout);
    if (!info) {
        return std::unexpected(makeError(
            ErrorCode::Timeout,
            std::format("the Bluetooth adapter did not answer within {} ms", timeout.count())));
    }
    return std::move(*info);
}

bool BleHost::isBleThread() const {
    const std::scoped_lock lock(m_startMutex);
    return m_thread.joinable() && std::this_thread::get_id() == m_threadId;
}

void BleHost::stop() {
    std::jthread thread;
    {
        const std::scoped_lock lock(m_startMutex);
        if (m_stopped) {
            return;
        }
        m_stopped = true;
        thread = std::move(m_thread);
    }
    if (!thread.joinable()) {
        return;
    }
    // Commands posted before stop() are processed first; later posts fail (cancelled).
    m_shared->shutdownRequested.store(true);
    m_shared->commands.close();
    m_shared->closes.close();
    m_shared->wake();
    if (!m_shared->waitFinished(stopWarningAfter)) {
        // ADR-0130: a running thread cannot be destroyed or abandoned; wait on (join below).
        qCWarning(log::lcBle) << "The BLE thread did not stop within" << stopWarningAfter.count()
                              << "ms; still waiting";
    }
    thread.join();
    if (!m_shared->threadFinished.load(std::memory_order_acquire)) {
        qCWarning(log::lcBle) << "The BLE thread ended without finishing its function";
    }
    qCDebug(log::lcBle) << "BLE thread stopped";
}

} // namespace hio::ble
