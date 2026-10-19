#include "hio/hw/ble/ble_scanner.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/hw/ble/ble_host.h"
#include "hio/hw/ble/types.h"

#include <QList>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QObject>
#include <QTimer>
#include <QtCore/qnamespace.h>
#include <QtCore/qtmetamacros.h>
#include <QtLogging>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace hio::ble {

namespace {

template <class... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
};

} // namespace

std::string truncateUtf8(std::string text, std::size_t maxBytes) {
    if (text.size() <= maxBytes) {
        return text;
    }
    std::size_t end = maxBytes;
    // Back to the start of the character that crosses the limit (continuation bytes 10xxxxxx).
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) {
        --end;
    }
    text.resize(end);
    return text;
}

BleScanner::BleScanner(BleHost& host, QObject* parent) : QObject(parent), m_host(&host) {
    m_subscription = BleHost::subscribe([this] {
        // BLE thread: one coalesced queued call (ADR-0080); never after detach().
        if (!m_wakePending.exchange(true)) {
            QMetaObject::invokeMethod(this, "drainEvents", Qt::QueuedConnection);
        }
    });
    m_signalTimer.setInterval(signalInterval);
    QObject::connect(&m_signalTimer, &QTimer::timeout, this, [this] { publish(); });
}

BleScanner::~BleScanner() {
    stop();
    m_subscription->detach();
}

Status BleScanner::start(ScanOptions options) {
    m_options = std::move(options);
    if (Status posted =
            m_host->post(StartScan{.subscriber = m_subscription, .duration = m_options.duration});
        !posted) {
        return posted;
    }
    m_scanning = true;
    m_signalTimer.start();
    qCInfo(log::lcBle) << "Scanning for" << m_options.duration.count() << "ms"
                       << (m_options.all ? "(all devices)" : "");
    return {};
}

void BleScanner::stop() {
    if (!m_scanning) {
        return;
    }
    m_scanning = false;
    if (Status posted = m_host->post(StopScan{.subscriber = m_subscription}); !posted) {
        qCDebug(log::lcBle) << "Scan stop not posted:" << toQString(posted.error().detail);
    }
    m_signalTimer.stop();
    publish();
}

void BleScanner::clear() {
    m_devices.clear();
    m_dirty = true;
}

void BleScanner::add(const Advertisement& advertisement) {
    const std::string profileId = m_options.matcher ? m_options.matcher(advertisement) : "";
    if (profileId.empty() && !m_options.all) {
        return;
    }
    const bool nus = std::ranges::find(advertisement.serviceUuids, std::string(nusServiceUuid)) !=
                     advertisement.serviceUuids.end();
    auto it = std::ranges::find(m_devices, advertisement.id, &BleDeviceInfo::id);
    if (it == m_devices.end()) {
        if (m_devices.size() >= maxDevices) {
            // Evict the device seen least recently.
            const auto oldest = std::ranges::min_element(m_devices, {}, &BleDeviceInfo::lastSeen);
            m_devices.erase(oldest);
        }
        m_devices.push_back(BleDeviceInfo{.id = advertisement.id,
                                          .name = {},
                                          .rssi = 0,
                                          .nusAdvertised = false,
                                          .profileId = {},
                                          .lastSeen = {}});
        it = std::prev(m_devices.end());
    }
    if (!advertisement.name.empty() || it->name.empty()) {
        it->name = truncateUtf8(advertisement.name, maxNameBytes);
    }
    it->rssi = advertisement.rssi;
    it->nusAdvertised = it->nusAdvertised || nus;
    if (!profileId.empty()) {
        it->profileId = profileId;
    }
    it->lastSeen = advertisement.seen;
    m_dirty = true;
}

void BleScanner::publish() {
    if (!m_dirty) {
        return;
    }
    m_dirty = false;
    std::ranges::stable_sort(
        m_devices, [](const BleDeviceInfo& a, const BleDeviceInfo& b) { return a.rssi > b.rssi; });
    emit devicesChanged(QList<BleDeviceInfo>(m_devices.begin(), m_devices.end()));
}

void BleScanner::drainEvents() {
    m_wakePending.store(false);
    // Bounded per call; the rest in the next (rescheduled) call.
    for (std::size_t i = 0; i < BleHost::eventQueueCapacity; ++i) {
        auto event = m_subscription->pop();
        if (!event) {
            break;
        }
        std::visit(Overloaded{
                       [this](const Advertisement& advertisement) { add(advertisement); },
                       [this](const ScanFinished& done) {
                           if (!m_scanning) {
                               return;
                           }
                           m_scanning = false;
                           m_signalTimer.stop();
                           // Sorted before the result even when nothing changed since the last
                           // signal.
                           std::ranges::stable_sort(
                               m_devices, [](const BleDeviceInfo& a, const BleDeviceInfo& b) {
                                   return a.rssi > b.rssi;
                               });
                           publish();
                           if (done.error) {
                               qCWarning(log::lcBle).noquote()
                                   << "Scan failed:" << toQString(done.error->detail);
                           }
                           emit finished(done.error);
                       },
                       [](const PairingResult&) {},
                   },
                   *event);
    }
    if (m_subscription->size() > 0 && !m_wakePending.exchange(true)) {
        QMetaObject::invokeMethod(this, "drainEvents", Qt::QueuedConnection);
    }
}

} // namespace hio::ble
