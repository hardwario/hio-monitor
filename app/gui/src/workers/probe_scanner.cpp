#include "workers/probe_scanner.h"

#include "gui_log.h"
#include "hio/device/probe_lock.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/probe_enumerator.h"
#include "hio/hw/jlink/types.h"

#include <QLoggingCategory>
#include <QMetaObject>
#include <QObject>
#include <QtCore/qnamespace.h>

#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

namespace hio::gui {

ProbeScanner::ProbeScanner(std::filesystem::path instanceRoot, std::filesystem::path lockDirectory,
                           std::chrono::milliseconds interval, QObject* parent)
    : QObject(parent), m_instanceRoot(std::move(instanceRoot)),
      m_lockDirectory(std::move(lockDirectory)), m_interval(interval),
      m_worker([this](const std::stop_token& stop) { run(stop); }) {}

ProbeScanner::~ProbeScanner() {
    m_worker.request_stop();
    m_wake.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void ProbeScanner::setLocatorOptions(jlink::LocatorOptions options) {
    const std::scoped_lock lock(m_mutex);
    m_options = std::move(options);
}

void ProbeScanner::acquire() {
    if (m_active.fetch_add(1) == 0) {
        scanNow();
    }
}

void ProbeScanner::release() {
    if (m_active.load() > 0) {
        m_active.fetch_sub(1);
    }
}

void ProbeScanner::scanNow() {
    {
        const std::scoped_lock lock(m_mutex);
        m_scanRequested = true;
    }
    m_wake.notify_all();
}

void ProbeScanner::run(const std::stop_token& stop) {
    while (!stop.stop_requested()) {
        jlink::LocatorOptions options;
        {
            std::unique_lock lock(m_mutex);
            const bool woken =
                m_wake.wait_for(lock, stop, m_interval, [this] { return m_scanRequested; });
            if (stop.stop_requested()) {
                return;
            }
            if (!woken && m_active.load() == 0) {
                continue; // periodic scans only while a picker is visible
            }
            m_scanRequested = false;
            options = m_options;
        }
        ProbeScan result = scan(options);
        {
            const std::scoped_lock lock(m_mutex);
            result.sequence = ++m_sequence;
            m_pending = std::move(result);
        }
        if (!m_deliverPending.exchange(true)) {
            // By name: Qt creates and consumes the queued call (see DeviceSession::wakeUp).
            QMetaObject::invokeMethod(this, "deliver", Qt::QueuedConnection);
        }
    }
}

ProbeScan ProbeScanner::scan(const jlink::LocatorOptions& options) const {
    ProbeScan result;
    auto location = jlink::locateLibrary(options);
    if (!location) {
        result.libraryError = std::move(location).error();
        return result;
    }
    result.libraryPath = location->path;
    auto enumeration = jlink::enumerateProbes(*location, m_instanceRoot);
    if (!enumeration) {
        result.libraryError = std::move(enumeration).error();
        return result;
    }
    result.libraryVersion = enumeration->libraryVersion;
    for (jlink::ProbeInfo& probe : enumeration->probes) {
        ScannedProbe scanned;
        scanned.lockOwner = device::ProbeLock::owner(probe.serialNumber, m_lockDirectory);
        scanned.probe = std::move(probe);
        result.probes.push_back(std::move(scanned));
    }
    return result;
}

void ProbeScanner::deliver() {
    m_deliverPending.store(false);
    std::optional<ProbeScan> result;
    {
        const std::scoped_lock lock(m_mutex);
        result.swap(m_pending);
    }
    if (!result) {
        return;
    }
    if (result->libraryError) {
        qCDebug(lcGui) << "Probe scan: no library:" << result->libraryError->detail.c_str();
    }
    m_last = std::move(result);
    scanned(*m_last); // signal
}

} // namespace hio::gui
