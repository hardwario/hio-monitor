#pragma once

// ProbeScanner - background J-Link probe enumeration for the probe pickers
// (plan/features/0004-m3-gui-shell.md, B3; ADR-0050, ADR-0058).
//
// One owned std::jthread for the application (owned by AppViewModel). While at least one picker is
// visible (acquire()/release(), an atomic count set from the GUI thread) it locates the JLinkARM
// library, enumerates the probes and reads the cross-process lock owner of every probe
// (ProbeLock::owner) every `interval` (2 s), and once at once on scanNow(). ADR-0093: the worker
// keeps one jlink::ProbeEnumerator (its own library copy, ADR-0030) loaded while a picker is
// visible, unloads it when the last picker goes away and loads a new one when the library location
// changes; a Refresh without a visible picker uses a copy only for that scan. A library that
// could not be loaded or could not list probes is remembered (path, size, modification time) and
// not loaded again on the 2 s tick; a Refresh, another location or a changed file retries (S17).
//
// Waiting (review S19): copies are loaded and unloaded under one process-wide lock in
// libs/hw/jlink (ADR-0093 amendment). A session's worker unloads its copy inside
// DeviceSession::close(), which the GUI thread joins (Disconnect, tab close, quit), so the GUI
// thread can wait for at most one library load of this scanner that is in progress (on Windows an
// antivirus scan can make a load take seconds). Loads are rare: when a picker becomes visible, on
// a location change and on a Refresh after a failure, never on the periodic tick (the copy is kept
// and failures are remembered). The scanner never takes the lock on the GUI thread. At most one
// scan runs at a time. It never touches the SessionRegistry (GUI-owned): the "in use by a tab"
// information is added by the pickers on the GUI thread.
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
#include "hio/hw/jlink/probe_enumerator.h"
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
    // Diagnostics and tests (ADR-0093): the worker holds a library copy now; how many copies it
    // has loaded so far.
    [[nodiscard]] bool libraryLoaded() const noexcept { return m_libraryLoaded.load(); }
    [[nodiscard]] int libraryLoads() const noexcept { return m_libraryLoads.load(); }
    // Load attempts, failed ones included.
    [[nodiscard]] int libraryLoadAttempts() const noexcept { return m_libraryLoadAttempts.load(); }

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
    // Worker thread. `forced`: a Refresh / a picker became visible (retries a failed library).
    [[nodiscard]] ProbeScan scan(const jlink::LocatorOptions& options,
                                 std::optional<jlink::ProbeEnumerator>& enumerator, bool forced);

    // The library file a load failed for (path, size, modification time; review S17).
    struct LibraryIdentity {
        std::filesystem::path path;
        std::uintmax_t size = 0;
        std::filesystem::file_time_type modified;
        [[nodiscard]] bool operator==(const LibraryIdentity& other) const = default;
    };
    struct FailedLibrary {
        LibraryIdentity identity;
        Error error;
    };

    const std::filesystem::path m_instanceRoot;
    const std::filesystem::path m_lockDirectory;
    const std::chrono::milliseconds m_interval;

    std::atomic<int> m_active{0};
    std::atomic<bool> m_deliverPending{false};
    std::atomic<bool> m_libraryLoaded{false};
    std::atomic<int> m_libraryLoads{0};
    std::atomic<int> m_libraryLoadAttempts{0};
    std::optional<FailedLibrary> m_failed; // worker thread only

    std::mutex m_mutex; // guards the members below
    std::condition_variable_any m_wake;
    bool m_scanRequested = false;
    bool m_activityChanged = false; // the last picker was released
    jlink::LocatorOptions m_options;
    std::optional<ProbeScan> m_pending; // the newest result not yet delivered
    std::uint64_t m_sequence = 0;

    std::optional<ProbeScan> m_last; // GUI thread only
    std::jthread m_worker;           // last member: started last, joined first
};

} // namespace hio::gui

Q_DECLARE_METATYPE(hio::gui::ProbeScan)
