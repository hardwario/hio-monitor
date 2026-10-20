#include "controller_closer.h"

#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/hw/ble/types.h"

#include <QEventLoop>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QtLogging>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace hio::ble {

ControllerCloser::ControllerCloser(std::chrono::milliseconds bound)
    : m_bound(bound), m_context(std::make_unique<QObject>()) {}

ControllerCloser::~ControllerCloser() {
    // Whatever is left goes with its owner; nothing is called back any more.
    for (Entry& entry : m_entries) {
        QObject::disconnect(entry.connection);
        if (entry.object) {
            entry.object->deleteLater();
        }
    }
    m_entries.clear();
    m_waiters.clear();
}

void ControllerCloser::close(QObject* object, const BleDeviceId& device, bool unconnected,
                             const Watch& watch) {
    if (object == nullptr) {
        return;
    }
    if (unconnected) {
        object->deleteLater();
        runWaiters();
        return;
    }
    const std::uint64_t serial = m_nextSerial++;
    Entry entry{.object = object, .device = device, .connection = {}, .serial = serial};
    entry.connection = watch(m_context.get(), [this, serial] { finish(serial, false); });
    m_entries.push_back(std::move(entry));
    QTimer::singleShot(m_bound, m_context.get(), [this, serial] { finish(serial, true); });
    qCDebug(log::lcBle) << "Waiting for the controller of" << toQString(device) << "to disconnect";
}

void ControllerCloser::finish(std::uint64_t serial, bool expired) {
    const auto it = std::ranges::find(m_entries, serial, &Entry::serial);
    if (it == m_entries.end()) {
        return; // finished already
    }
    if (expired) {
        qCWarning(log::lcBle) << "The Bluetooth controller of" << toQString(it->device)
                              << "did not disconnect within" << m_bound.count()
                              << "ms; deleting it";
    }
    QObject::disconnect(it->connection);
    if (it->object) {
        it->object->deleteLater();
    }
    m_entries.erase(it);
    runWaiters();
}

bool ControllerCloser::isClosing(const BleDeviceId& device) const {
    return std::ranges::any_of(
        m_entries, [&device](const Entry& entry) { return sameDeviceId(entry.device, device); });
}

void ControllerCloser::whenClosed(const BleDeviceId& device, std::function<void()> then) {
    if (!isClosing(device)) {
        then();
        return;
    }
    m_waiters.push_back(Waiter{.device = device, .then = std::move(then)});
}

void ControllerCloser::runWaiters() {
    std::vector<Waiter> ready;
    std::erase_if(m_waiters, [this, &ready](Waiter& waiter) {
        if (isClosing(waiter.device)) {
            return false;
        }
        ready.push_back(std::move(waiter));
        return true;
    });
    for (Waiter& waiter : ready) {
        waiter.then();
    }
}

void ControllerCloser::waitAll() {
    if (m_entries.empty()) {
        return;
    }
    QEventLoop loop;
    QTimer poll;
    const auto deadline = std::chrono::steady_clock::now() + m_bound + std::chrono::seconds(1);
    QObject::connect(&poll, &QTimer::timeout, &loop, [this, &loop, deadline] {
        if (m_entries.empty() || std::chrono::steady_clock::now() > deadline) {
            loop.quit();
        }
    });
    poll.start(std::chrono::milliseconds(10));
    // The entries' own timers end them at the latest after the bound.
    loop.exec();
}

} // namespace hio::ble
