#include "workers/probe_scanner.h"

#include "gui_log.h"
#include "hio/core/error.h"
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
#include <system_error>
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
    if (m_active.load() > 0 && m_active.fetch_sub(1) == 1) {
        // The last picker went away: the worker unloads its library copy (ADR-0093).
        {
            const std::scoped_lock lock(m_mutex);
            m_activityChanged = true;
        }
        m_wake.notify_all();
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
    // ADR-0093: one library copy, owned by this thread, kept while a picker is visible.
    std::optional<jlink::ProbeEnumerator> enumerator;
    while (!stop.stop_requested()) {
        jlink::LocatorOptions options;
        bool requested = false;
        {
            std::unique_lock lock(m_mutex);
            const bool woken = m_wake.wait_for(
                lock, stop, m_interval, [this] { return m_scanRequested || m_activityChanged; });
            if (stop.stop_requested()) {
                break;
            }
            m_activityChanged = false;
            requested = woken && m_scanRequested;
            m_scanRequested = false;
            options = m_options;
        }
        if (!requested && m_active.load() == 0) {
            enumerator.reset(); // periodic scans only while a picker is visible
            m_libraryLoaded.store(false);
            continue;
        }
        ProbeScan result = scan(options, enumerator, requested);
        if (m_active.load() == 0) {
            enumerator.reset(); // a Refresh without a visible picker: no copy is kept
        }
        m_libraryLoaded.store(enumerator.has_value());
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
    enumerator.reset();
    m_libraryLoaded.store(false);
}

ProbeScan ProbeScanner::scan(const jlink::LocatorOptions& options,
                             std::optional<jlink::ProbeEnumerator>& enumerator, bool forced) {
    ProbeScan result;
    auto location = jlink::locateLibrary(options);
    if (!location) {
        enumerator.reset();
        result.libraryError = std::move(location).error();
        return result;
    }
    result.libraryPath = location->path;
    // Review S17: a library that could not be loaded or could not list probes is not copied and
    // loaded again every 2 s; only a Refresh (forced), another location or a changed file retries.
    std::error_code ec;
    LibraryIdentity identity{.path = location->path,
                             .size = std::filesystem::file_size(location->path, ec),
                             .modified = std::filesystem::last_write_time(location->path, ec)};
    if (!forced && m_failed && m_failed->identity == identity) {
        result.libraryError = m_failed->error;
        return result;
    }
    const auto remember = [this, &identity](const Error& error) {
        m_failed = FailedLibrary{.identity = std::move(identity), .error = error};
    };
    // Another library (the setting or the environment changed): load that one instead.
    if (enumerator && enumerator->location().path != location->path) {
        enumerator.reset();
    }
    if (!enumerator) {
        m_libraryLoadAttempts.fetch_add(1);
        auto loaded = jlink::ProbeEnumerator::load(*location, m_instanceRoot);
        if (!loaded) {
            remember(loaded.error());
            result.libraryError = std::move(loaded).error();
            return result;
        }
        enumerator.emplace(std::move(*loaded));
        m_libraryLoads.fetch_add(1);
    }
    auto probes = enumerator->enumerate();
    if (!probes) {
        enumerator.reset(); // a fresh copy on the next retry
        remember(probes.error());
        result.libraryError = std::move(probes).error();
        return result;
    }
    m_failed.reset();
    result.libraryVersion = enumerator->libraryVersion();
    for (jlink::ProbeInfo& probe : *probes) {
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
