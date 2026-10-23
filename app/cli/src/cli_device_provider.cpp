#include "cli_device_provider.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/device_session.h"
#include "hio/device/probe_lock.h"
#include "hio/device/profile.h"
#include "hio/device/session_registry.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/probe_enumerator.h"
#include "hio/hw/jlink/types.h"
#include "hio/hw/nrfutil/nrfutil.h"
#include "hio/mcp/offload.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/tools/device_provider.h"

#include <QLoggingCategory>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <Qt>
#include <QtCore/qtmetamacros.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::cli {

namespace {

using hio::mcp::tools::DeviceEntry;
using hio::mcp::tools::DeviceRef;

constexpr std::string_view jlinkPrefix = "jlink:";
constexpr std::string_view defaultProfileId = "chester";

Error deviceNotOpen(std::string_view id) {
    return makeError(ErrorCode::McpDeviceNotOpen, std::string(id));
}

// What list() shows of a probe (serial, product, owner app and PID).
template <class Probe> bool sameProbes(const std::vector<Probe>& a, const std::vector<Probe>& b) {
    return std::ranges::equal(a, b, [](const Probe& x, const Probe& y) {
        const auto owner = [](const Probe& probe) {
            return probe.otherProcess
                       ? std::optional(std::pair(probe.otherProcess->app, probe.otherProcess->pid))
                       : std::nullopt;
        };
        return x.serialNumber == y.serialNumber && x.product == y.product && owner(x) == owner(y);
    });
}

// Review F3: owned (through a shared_ptr) by the `done` functor of one refresh job. The Offloader
// destroys `done` on the owner thread without calling it when the job is dropped (handle
// cancelled, stopAll(), the offloader destroyed); the guard then posts `dropped` as a queued call
// to `context`, so the waiters never run inside the Offloader's own loops. Qt discards the call
// when `context` is destroyed first.
class RefreshDropGuard final {
public:
    RefreshDropGuard(QObject& context, std::function<void()> dropped)
        : m_context(&context), m_dropped(std::move(dropped)) {}
    ~RefreshDropGuard() {
        if (!m_delivered && !m_context.isNull() && m_dropped) {
            QMetaObject::invokeMethod(m_context.data(), std::move(m_dropped), Qt::QueuedConnection);
        }
    }
    RefreshDropGuard(const RefreshDropGuard&) = delete;
    RefreshDropGuard& operator=(const RefreshDropGuard&) = delete;
    RefreshDropGuard(RefreshDropGuard&&) = delete;
    RefreshDropGuard& operator=(RefreshDropGuard&&) = delete;

    void delivered() noexcept { m_delivered = true; }

private:
    QPointer<QObject> m_context;
    std::function<void()> m_dropped;
    bool m_delivered = false;
};

} // namespace

CliDeviceProvider::CliDeviceProvider(LibraryLocator locator, CliProviderOptions options,
                                     device::RegistryOptions registry)
    : m_locator(std::move(locator)), m_options(std::move(options)),
      m_registry(std::make_unique<device::SessionRegistry>(std::move(registry))) {}

CliDeviceProvider::~CliDeviceProvider() { closeAll(); }

std::optional<std::uint32_t> CliDeviceProvider::probeSerialOf(std::string_view id) {
    if (!id.starts_with(jlinkPrefix)) {
        return std::nullopt;
    }
    const std::string_view digits = id.substr(jlinkPrefix.size());
    if (digits.empty() || digits.front() == '0') {
        return std::nullopt;
    }
    constexpr std::size_t maxDigits = 10; // 4294967295
    if (digits.size() > maxDigits) {
        return std::nullopt;
    }
    std::uint64_t serial = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        serial = (serial * 10) + static_cast<std::uint64_t>(c - '0');
    }
    if (serial > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(serial);
}

Result<jlink::LibraryLocation> CliDeviceProvider::location() {
    if (!m_location) {
        auto located = m_locator();
        if (!located) {
            return std::unexpected(std::move(located).error());
        }
        m_location = std::move(*located);
    }
    return *m_location;
}

std::vector<DeviceEntry> CliDeviceProvider::list() {
    std::vector<DeviceEntry> entries;
    entries.reserve(m_sessions.size() + m_probes.size());
    for (const auto& [id, entry] : m_sessions) {
        entries.push_back(DeviceEntry{.id = id,
                                      .kind = "jlink",
                                      .displayName = entry.session->identity().displayName,
                                      .profileId = std::string(entry.session->profile().id),
                                      .state = DeviceEntry::State::Open,
                                      .owner = std::nullopt,
                                      .connection = entry.session->connection(),
                                      .openedByMcp = true});
    }
    for (const CachedProbe& probe : m_probes) {
        std::string id = std::format("{}{}", jlinkPrefix, probe.serialNumber);
        if (m_sessions.contains(id)) {
            continue;
        }
        entries.push_back(DeviceEntry{
            .id = std::move(id),
            .kind = "jlink",
            .displayName = probe.product.empty()
                               ? std::format("J-Link {}", probe.serialNumber)
                               : std::format("J-Link {} ({})", probe.serialNumber, probe.product),
            .profileId = {},
            .state = probe.otherProcess ? DeviceEntry::State::InUseByOtherProcess
                                        : DeviceEntry::State::Available,
            .owner = probe.otherProcess,
            .connection = std::nullopt,
            .openedByMcp = false});
    }
    return entries;
}

void CliDeviceProvider::refresh(std::function<void(Status)> done) {
    const bool first = !std::exchange(m_refreshRequested, true);
    if (!m_refreshWaiters.empty()) { // joins the running enumeration
        if (m_refreshWaiters.size() >= maxPendingRefreshes) {
            done(std::unexpected(
                makeError(ErrorCode::McpTooManyRequests, "too many waiting probe enumerations")));
            return;
        }
        m_refreshWaiters.push_back(std::move(done));
        return;
    }
    if (m_offloader.isNull()) {
        done(std::unexpected(
            makeError(ErrorCode::InternalUnexpected, "no offloader for the probe enumeration")));
        return;
    }
    auto located = location();
    if (!located) {
        done(std::unexpected(std::move(located).error()));
        return;
    }
    const std::uint64_t number = m_refreshJobNumber + 1;
    auto guard =
        std::make_shared<RefreshDropGuard>(m_notifier, [this, number] { refreshDropped(number); });
    // The job owns copies of everything it needs (ADR-0162); it never touches the provider.
    auto job = m_offloader->submit<Enumeration>(
        m_notifier,
        [location = *located, instanceRoot = m_registry->options().instanceRoot,
         lockDirectory =
             m_registry->options().lockDirectory](const std::stop_token&) -> Enumeration {
            // Review F2: the stop token is ignored on purpose. The JLinkARM library cannot
            // interrupt an enumeration; it is bounded (about 2 s), and Offloader::stopAll() /
            // server shutdown wait for it at most that long.
            auto enumeration = jlink::enumerateProbes(location, instanceRoot);
            if (!enumeration) {
                return std::unexpected(std::move(enumeration).error());
            }
            std::vector<FoundProbe> found;
            found.reserve(enumeration->probes.size());
            for (jlink::ProbeInfo& probe : enumeration->probes) {
                std::optional<device::ProbeLockOwner> owner =
                    device::ProbeLock::owner(probe.serialNumber, lockDirectory);
                found.push_back(
                    FoundProbe{.probe = std::move(probe), .lockOwner = std::move(owner)});
            }
            return found;
        },
        [this, guard](Enumeration found) {
            guard->delivered();
            enumerated(std::move(found));
        });
    if (!job) {
        guard->delivered(); // nothing was dropped: the error goes to `done` here
        done(std::unexpected(std::move(job).error()));
        return;
    }
    m_refreshJobNumber = number;
    m_refreshJob = std::move(*job);
    m_firstScanPending = first;
    m_refreshWaiters.push_back(std::move(done));
}

void CliDeviceProvider::refreshDropped(std::uint64_t job) {
    if (job != m_refreshJobNumber || m_refreshWaiters.empty()) {
        return; // not the running refresh (already finished)
    }
    m_refreshJob = {};
    finishRefresh(std::unexpected(makeError(ErrorCode::Cancelled, "probe enumeration dropped")));
}

void CliDeviceProvider::finishRefresh(const Status& outcome) {
    m_firstScanPending = false;
    for (auto& waiter : std::exchange(m_refreshWaiters, {})) {
        waiter(outcome);
    }
}

void CliDeviceProvider::enumerated(Enumeration found) {
    m_refreshJob = {};
    Status outcome;
    if (found) {
        // Merged on the owner thread: a probe of one of this process's sessions is not "used by
        // another process" even though its lock file names this process.
        std::vector<CachedProbe> probes;
        probes.reserve(found->size());
        for (FoundProbe& item : *found) {
            const bool ours = m_registry->sessionForProbe(item.probe.serialNumber) != nullptr;
            probes.push_back(
                CachedProbe{.serialNumber = item.probe.serialNumber,
                            .product = std::move(item.probe.product),
                            .otherProcess = ours ? std::nullopt : std::move(item.lockOwner)});
        }
        const bool changed = !sameProbes(probes, m_probes);
        m_probes = std::move(probes);
        if (changed) {
            emit m_notifier.devicesChanged();
        }
    } else {
        outcome = std::unexpected(std::move(found).error());
    }
    finishRefresh(outcome);
}

void CliDeviceProvider::open(const hio::mcp::tools::OpenRequest& request,
                             std::function<void(hio::Result<DeviceRef>)> done) {
    if (auto existing = find(request.id)) {
        done(*existing);
        return;
    }
    auto opened = openSession(request);
    if (opened) {
        emit m_notifier.devicesChanged();
    }
    done(std::move(opened));
}

Result<DeviceRef> CliDeviceProvider::openSession(const hio::mcp::tools::OpenRequest& request) {
    const std::optional<std::uint32_t> serial = probeSerialOf(request.id);
    if (!serial) {
        return std::unexpected(deviceNotOpen(request.id));
    }
    const std::string_view profileId =
        request.profileId.empty() ? defaultProfileId : std::string_view(request.profileId);
    const device::DeviceProfile* profile = device::ProfileRegistry::find(profileId);
    if (profile == nullptr || profile->targets.empty()) {
        Error error = makeError(ErrorCode::UsageInvalidArgument, std::string(profileId));
        error.message = std::format("Unknown device profile \"{}\".", profileId);
        return std::unexpected(std::move(error));
    }
    const device::TargetProfile* target = request.targetId.empty()
                                              ? &profile->targets.front()
                                              : device::findTarget(*profile, request.targetId);
    if (target == nullptr) {
        Error error = makeError(ErrorCode::UsageInvalidArgument, request.targetId);
        error.message =
            std::format("The profile {} has no target \"{}\".", profileId, request.targetId);
        return std::unexpected(std::move(error));
    }
    auto located = location();
    if (!located) {
        return std::unexpected(std::move(located).error());
    }

    auto consoleLog =
        std::make_shared<device::ConsoleLog>(device::ConsoleLog::defaultCapacityPerChannel);
    device::SessionOptions options;
    options.profile = profile;
    options.targetId = target->id; // static profile data
    options.probeSn = serial;
    if (request.speedKhz) {
        options.speedKhz = request.speedKhz;
    } else {
        options.speedKhz = m_options.speedGiven ? m_options.speedKhz : target->defaultSpeedKhz;
    }
    options.latencyMs = target->defaultLatencyMs;
    options.autoReconnect = request.autoReconnect.value_or(m_options.autoReconnect);
    options.consoleLog = consoleLog;
    options.consoleFileDir = m_options.consoleFileDir;
    if (m_options.nrfUtil) {
        options.nrfUtil = nrfutil::defaultLocatorOptions(m_options.nrfUtil, nullptr);
    }
    if (const std::optional<std::string> warning = device::slowSpeedWarning(options.speedKhz)) {
        qCWarning(log::lcMcp).noquote() << toQString(*warning);
    }
    auto session = device::DeviceSession::open(*m_registry, *located, std::move(options));
    if (!session) {
        return std::unexpected(std::move(session).error());
    }
    device::DeviceSession* jlink = session->get();
    DeviceRef ref{.console = jlink, .jlink = jlink};
    const std::string key = std::format("{}{}", jlinkPrefix, *serial);
    qCInfo(log::lcMcp).noquote() << "MCP: opened" << toQString(key) << "target"
                                 << toQString(target->id) << "console file"
                                 << toQString(toUtf8(jlink->consoleFile()));
    m_sessions.insert_or_assign(
        key, Entry{.session = std::move(*session), .jlink = jlink, .log = std::move(consoleLog)});
    return ref;
}

Status CliDeviceProvider::close(std::string_view id, const hio::mcp::RequestContext& /*context*/) {
    const auto it = m_sessions.find(id);
    if (it == m_sessions.end()) {
        return std::unexpected(deviceNotOpen(id));
    }
    Entry entry = std::move(it->second);
    m_sessions.erase(it);
    entry.session.reset(); // closes the session: joins its worker, releases the probe lock
    qCInfo(log::lcMcp).noquote() << "MCP: closed" << toQString(id);
    emit m_notifier.devicesChanged();
    return {};
}

void CliDeviceProvider::closeAll() {
    if (m_sessions.empty()) {
        return;
    }
    std::map<std::string, Entry, std::less<>> sessions = std::move(m_sessions);
    m_sessions.clear();
    for (auto& [id, entry] : sessions) {
        entry.session.reset();
        qCInfo(log::lcMcp).noquote() << "MCP: closed" << toQString(id);
    }
    emit m_notifier.devicesChanged();
}

std::optional<DeviceRef> CliDeviceProvider::find(std::string_view id) {
    const auto it = m_sessions.find(id);
    if (it == m_sessions.end()) {
        return std::nullopt;
    }
    return DeviceRef{.console = it->second.session.get(), .jlink = it->second.jlink};
}

void CliDeviceProvider::beginBusy(std::string_view id, QString text,
                                  std::function<void()> /*cancel*/) {
    qCInfo(log::lcMcp).noquote() << "MCP:" << toQString(id) << "busy:" << text;
}

void CliDeviceProvider::endBusy(std::string_view id) {
    qCInfo(log::lcMcp).noquote() << "MCP:" << toQString(id) << "no longer busy";
}

void CliDeviceProvider::marker(std::string_view id, QString text) {
    if (const auto it = m_sessions.find(id); it != m_sessions.end()) {
        it->second.log->appendMarker(device::Channel::Shell, toStdString(text));
    }
}

void CliDeviceProvider::setAiCommandHint(std::string_view id, bool active) {
    qCDebug(log::lcMcp).noquote() << "MCP:" << toQString(id) << "AI command" << active;
}

} // namespace hio::cli
