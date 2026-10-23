#pragma once

// CliDeviceProvider - the IDeviceProvider of `hio mcp` (plan/features/0013 "Device and consent
// seams", ADR-0098, ADR-0156).
//
// It owns the sessions it opened (std::unique_ptr<ConsoleSession>, at most
// SessionRegistry::maxSessions; the registry refuses more with internal.session_limit). Each
// session gets its own ConsoleLog (ConsoleLog::defaultCapacityPerChannel = 100 000 lines per
// channel, ADR-0063) and writes its console file by the M1 rules, into --console-file-dir when
// given (ADR-0156). Devices: the J-Link probes of the last enumeration, ids "jlink:<serial>"
// (canonical decimal). Bluetooth devices come with phase D (plan 0013 D2); until then "ble:" ids
// are unknown (mcp.device_not_open).
//
// Enumeration (ADR-0162): refresh() runs jlink::enumerateProbes (its own short-lived library copy)
// and the lock-owner lookup (ProbeLock::owner) as one job on the server's Offloader, with copied
// data only (library location, instance root, lock directory). The result comes back to the owner
// thread, where it is merged with this process's sessions (SessionRegistry::sessionForProbe) and
// cached; list() only reads the cache. `hio mcp` sets the offloader after the server is built
// (setOffloader) and starts one refresh; until that first enumeration has finished,
// firstScanPending() is true and list_devices waits for it (review V1). Without an offloader
// refresh() fails with internal.unexpected. The enumeration cannot be interrupted (the JLinkARM
// library offers no cancellation): it ignores its stop token and is bounded (about 2 s), so a
// server stop (Offloader::stopAll) waits for it at most that long (review F2). When the job is
// dropped (Offloader handle cancelled, stopAll(), the offloader destroyed) its `done` is never
// called; the waiters are then failed with `cancelled` from a queued call (review F3).
//
// Connect defaults: the `hio mcp` options --jlink-speed (else the target's default speed) and
// --auto-reconnect; the profile "chester" and its first target when the request leaves them out.
// beginBusy / endBusy / setAiCommandHint only log (the CLI has no UI); marker() appends to the
// session's ConsoleLog (Shell channel). devicesChanged() follows every open and close.
//
// Thread safety: owner thread only (ADR-0097), except the offloaded enumeration job, which touches
// no member. close() and closeAll() destroy sessions
// synchronously (the worker is joined), so they must not run inside a session's signal.

#include "hio/core/result.h"
#include "hio/device/console_log.h"
#include "hio/device/console_session.h"
#include "hio/device/device_session.h"
#include "hio/device/probe_lock.h"
#include "hio/device/session_registry.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/types.h"
#include "hio/mcp/offload.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/tools/device_provider.h"

#include <QPointer>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hio::cli {

struct CliProviderOptions {
    // --jlink-speed: when given, the speed for `connect` without speed_khz (nullopt = automatic);
    // when not given, the target profile's default speed.
    bool speedGiven = false;
    std::optional<int> speedKhz;
    bool autoReconnect = false; // --auto-reconnect: `connect` without auto_reconnect
    std::optional<std::filesystem::path> consoleFileDir; // --console-file-dir (ADR-0156)
    std::optional<std::filesystem::path> nrfUtil;        // --nrfutil (modem updates, ADR-0016)
};

class CliDeviceProvider final : public hio::mcp::tools::IDeviceProvider {
public:
    // Waiters of one running refresh (list_devices calls are bounded by the server's in-flight
    // limit; one more for the start-up refresh).
    static constexpr std::size_t maxPendingRefreshes = 64;

    // Locates the J-Link library (called on the first enumeration or open; a success is kept).
    using LibraryLocator = std::function<Result<jlink::LibraryLocation>()>;

    CliDeviceProvider(LibraryLocator locator, CliProviderOptions options,
                      device::RegistryOptions registry = device::RegistryOptions::defaults());
    ~CliDeviceProvider() override; // closeAll()
    CliDeviceProvider(const CliDeviceProvider&) = delete;
    CliDeviceProvider& operator=(const CliDeviceProvider&) = delete;
    CliDeviceProvider(CliDeviceProvider&&) = delete;
    CliDeviceProvider& operator=(CliDeviceProvider&&) = delete;

    // The open sessions, then the probes of the last enumeration (never enumerates).
    [[nodiscard]] std::vector<hio::mcp::tools::DeviceEntry> list() override;
    void refresh(std::function<void(hio::Status)> done) override;
    [[nodiscard]] bool firstScanPending() const override { return m_firstScanPending; }
    // The server's offloader for refresh() (ADR-0162). Held by a QPointer: once the offloader is
    // destroyed refresh() behaves as without one.
    void setOffloader(hio::mcp::Offloader* offloader) { m_offloader = offloader; }
    void open(const hio::mcp::tools::OpenRequest& request,
              std::function<void(hio::Result<hio::mcp::tools::DeviceRef>)> done) override;
    [[nodiscard]] hio::Status close(std::string_view id,
                                    const hio::mcp::RequestContext& context) override;
    [[nodiscard]] std::optional<hio::mcp::tools::DeviceRef> find(std::string_view id) override;
    void beginBusy(std::string_view id, QString text, std::function<void()> cancel) override;
    void endBusy(std::string_view id) override;
    void marker(std::string_view id, QString text) override;
    void setAiCommandHint(std::string_view id, bool active) override;
    [[nodiscard]] hio::mcp::tools::DeviceProviderNotifier& notifier() override {
        return m_notifier;
    }

    // Closes every session (shutdown); devicesChanged() once when any was open.
    void closeAll();
    [[nodiscard]] std::size_t sessionCount() const noexcept { return m_sessions.size(); }

    // "jlink:<serial>" -> the serial (decimal without leading zeros, 1..4294967295), else nullopt.
    [[nodiscard]] static std::optional<std::uint32_t> probeSerialOf(std::string_view id);

private:
    struct Entry {
        std::unique_ptr<device::ConsoleSession> session;
        device::DeviceSession* jlink = nullptr; // non-owning view of `session` (J-Link sessions)
        std::shared_ptr<device::ConsoleLog> log;
    };
    // A probe of the last enumeration (no pointer to sessions is kept).
    struct CachedProbe {
        std::uint32_t serialNumber = 0;
        std::string product;
        std::optional<device::ProbeLockOwner> otherProcess;
    };
    // What the offloaded job finds: the probe and its lock owner (any process, this one included).
    struct FoundProbe {
        jlink::ProbeInfo probe;
        std::optional<device::ProbeLockOwner> lockOwner;
    };
    using Enumeration = Result<std::vector<FoundProbe>>;

    void enumerated(Enumeration found);
    // Review F3: the job `job` was dropped without delivering; fails its waiters with `cancelled`.
    void refreshDropped(std::uint64_t job);
    // Ends the running refresh: the first scan is over, every waiter gets `outcome` once.
    void finishRefresh(const Status& outcome);

    [[nodiscard]] Result<jlink::LibraryLocation> location();
    [[nodiscard]] Result<hio::mcp::tools::DeviceRef>
    openSession(const hio::mcp::tools::OpenRequest& request);

    LibraryLocator m_locator;
    std::optional<jlink::LibraryLocation> m_location;
    CliProviderOptions m_options;
    hio::mcp::tools::DeviceProviderNotifier m_notifier;
    std::vector<CachedProbe> m_probes;
    QPointer<hio::mcp::Offloader> m_offloader;
    hio::mcp::Offloader::Handle m_refreshJob; // the running enumeration
    std::uint64_t m_refreshJobNumber = 0;     // counts submitted enumerations (review F3)
    bool m_refreshRequested = false;          // refresh() was called at least once
    bool m_firstScanPending = false;          // the enumeration of the first refresh() runs (V1)
    // `done` functors of the running refresh, at most maxPendingRefreshes.
    std::vector<std::function<void(hio::Status)>> m_refreshWaiters;
    // The registry outlives the sessions (declared before them).
    std::unique_ptr<device::SessionRegistry> m_registry;
    std::map<std::string, Entry, std::less<>> m_sessions;
};

} // namespace hio::cli
