#pragma once

// ProbeScanner - background J-Link probe enumeration for the probe pickers
// (plan/features/0004-m3-gui-shell.md, B3; ADR-0050, ADR-0058).
//
// One owned std::jthread for the application (owned by AppViewModel). While at least one picker is
// visible (acquire()/release(), an atomic count set from the GUI thread) it locates the JLinkARM
// library, enumerates the probes with a separate short-lived library copy (jlink::enumerateProbes,
// ADR-0030) and reads the cross-process lock owner of every probe (ProbeLock::owner) every
// `interval` (2 s), and once at once on scanNow(). At most one scan runs at a time. It never
// touches the SessionRegistry (GUI-owned): the "in use by a tab" information is added by the
// pickers on the GUI thread.
//
// Results are handed over in a mutex-protected slot; the worker wakes the GUI thread with one
// coalesced queued call by name (the DeviceSession pattern, ADR-0031), and `scanned` is emitted
// there. The stop token is honoured between scans and the thread is joined in the destructor
// (one enumeration, a few hundred ms, may still finish first).
//
// Thread safety: the public functions are called on the GUI thread (the owner).

#include "hio/core/error.h"
#include "hio/device/probe_lock.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/types.h"

#include <QMetaType>
#include <QObject>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace hio::gui {

struct ScannedProbe {
    jlink::ProbeInfo probe;
    std::optional<device::ProbeLockOwner> lockOwner; // another process holds the probe lock
};

struct ProbeScan {
    std::uint64_t sequence = 0;        // counts from 1
    std::optional<Error> libraryError; // locate or load failed (dependency.*, probe.open_failed)
    std::filesystem::path libraryPath; // the located library ("" when not found)
    std::uint32_t libraryVersion = 0;  // read from the loaded library (0 = unknown)
    std::vector<ScannedProbe> probes;
};

class ProbeScanner : public QObject {
    Q_OBJECT

public:
    static constexpr std::chrono::milliseconds defaultInterval{2000};

    ProbeScanner(std::filesystem::path instanceRoot, std::filesystem::path lockDirectory,
                 std::chrono::milliseconds interval = defaultInterval, QObject* parent = nullptr);
    ~ProbeScanner() override; // stops and joins the worker
    ProbeScanner(const ProbeScanner&) = delete;
    ProbeScanner& operator=(const ProbeScanner&) = delete;
    ProbeScanner(ProbeScanner&&) = delete;
    ProbeScanner& operator=(ProbeScanner&&) = delete;

    // The locator options for the next scans (read from the settings on the GUI thread).
    void setLocatorOptions(jlink::LocatorOptions options);

    // A picker became visible / hidden. The first acquire() scans at once.
    void acquire();
    void release();
    [[nodiscard]] int activeCount() const noexcept { return m_active.load(); }

    // Scans as soon as possible (Refresh), also when no picker is visible.
    void scanNow();

    // The last result (nullopt before the first scan finished).
    [[nodiscard]] const std::optional<ProbeScan>& last() const noexcept { return m_last; }

signals:
    void scanned(const hio::gui::ProbeScan& scan);

private:
    // GUI thread: invoked (queued, by name) by the worker.
    Q_INVOKABLE void deliver();

    void run(const std::stop_token& stop);
    [[nodiscard]] ProbeScan scan(const jlink::LocatorOptions& options) const;

    const std::filesystem::path m_instanceRoot;
    const std::filesystem::path m_lockDirectory;
    const std::chrono::milliseconds m_interval;

    std::atomic<int> m_active{0};
    std::atomic<bool> m_deliverPending{false};

    std::mutex m_mutex; // guards the members below
    std::condition_variable_any m_wake;
    bool m_scanRequested = false;
    jlink::LocatorOptions m_options;
    std::optional<ProbeScan> m_pending; // the newest result not yet delivered
    std::uint64_t m_sequence = 0;

    std::optional<ProbeScan> m_last; // GUI thread only
    std::jthread m_worker;           // last member: started last, joined first
};

} // namespace hio::gui

Q_DECLARE_METATYPE(hio::gui::ProbeScan)
