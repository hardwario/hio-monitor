#include "hio/device/session_registry.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/ble_lock.h"
#include "hio/device/console_file_sink.h"
#include "hio/device/console_session.h"
#include "hio/device/device_session.h"
#include "hio/device/probe_lock.h"
#include "hio/device/session_events.h"
#include "hio/device/session_key.h"
#include "hio/hw/ble/types.h"
#include "hio/hw/jlink/jlink_library.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/probe_enumerator.h"
#include "hio/hw/jlink/types.h"
#include "session_internal.h"

#include <QCoreApplication>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <qobject.h>
#include <utility>
#include <variant>
#include <vector>

namespace hio::device {

RegistryOptions RegistryOptions::defaults() {
    return RegistryOptions{.instanceRoot = jlink::JLinkLibrary::defaultInstanceRoot(),
                           .lockDirectory = ProbeLock::defaultDirectory(),
                           .consoleDirectory = ConsoleFileSink::defaultDirectory()};
}

SessionRegistry::SessionRegistry(RegistryOptions options) : m_options(std::move(options)) {
    jlink::JLinkLibrary::cleanupStaleInstances(m_options.instanceRoot);
}

SessionRegistry::~SessionRegistry() {
    // close() removes the session from m_sessions.
    while (!m_sessions.empty()) {
        m_sessions.back()->close();
    }
}

Result<ProbeStatusList>
SessionRegistry::enumerateProbes(const jlink::LibraryLocation& location) const {
    auto enumeration = jlink::enumerateProbes(location, m_options.instanceRoot);
    if (!enumeration) {
        return std::unexpected(std::move(enumeration).error());
    }
    ProbeStatusList list;
    list.libraryVersion = enumeration->libraryVersion;
    for (jlink::ProbeInfo& probe : enumeration->probes) {
        ProbeStatus status;
        status.usedBySession = sessionForProbe(probe.serialNumber);
        if (status.usedBySession == nullptr) {
            status.usedByOtherProcess =
                ProbeLock::owner(probe.serialNumber, m_options.lockDirectory);
        }
        status.probe = std::move(probe);
        list.probes.push_back(std::move(status));
    }
    return list;
}

Result<std::vector<ProbeStatus>>
SessionRegistry::listProbes(const jlink::LibraryLocation& location) const {
    auto list = enumerateProbes(location);
    if (!list) {
        return std::unexpected(std::move(list).error());
    }
    return std::move(list->probes);
}

const DeviceSession* SessionRegistry::sessionForProbe(std::uint32_t serialNumber) const {
    return qobject_cast<const DeviceSession*>(sessionFor(ProbeKey{.sn = serialNumber}));
}

const ConsoleSession* SessionRegistry::sessionFor(const SessionKey& key) const {
    const auto it = std::ranges::find_if(m_sessions, [&key](const ConsoleSession* session) {
        if (const auto* probe = std::get_if<ProbeKey>(&key)) {
            const auto* device = qobject_cast<const DeviceSession*>(session);
            return device != nullptr &&
                   (device->probe().serialNumber == probe->sn ||
                    detail::SessionInternals::requestedSerial(*device) == probe->sn);
        }
        const SessionIdentity identity = session->identity();
        return identity.transport == Transport::Ble && identity.id == std::get<BleKey>(key).id;
    });
    return it == m_sessions.end() ? nullptr : *it;
}

BleUse SessionRegistry::bleInUse(const ble::BleDeviceId& id) const {
    BleUse use;
    use.session = sessionFor(BleKey{.id = id});
    if (use.session == nullptr) {
        use.otherProcess = BleLock::owner(id, m_options.lockDirectory);
    }
    return use;
}

Status SessionRegistry::checkAvailable(std::optional<std::uint32_t> serialNumber) const {
    return checkAvailable(serialNumber ? std::optional<SessionKey>(ProbeKey{.sn = *serialNumber})
                                       : std::nullopt);
}

Status SessionRegistry::checkAvailable(const std::optional<SessionKey>& key) const {
    if (m_sessions.size() >= maxSessions) {
        return std::unexpected(
            makeError(ErrorCode::InternalSessionLimit,
                      std::format("{} of {} sessions are open", m_sessions.size(), maxSessions)));
    }
    if (key && std::holds_alternative<BleKey>(*key) && sessionFor(*key) != nullptr) {
        const ble::BleDeviceId& id = std::get<BleKey>(*key).id;
        Error error = makeError(ErrorCode::BleInUseByOtherProcess,
                                "the device is used by another session of this process");
        error.message = QCoreApplication::translate(
                            "hio::device", "The device %1 is already used by this application.")
                            .arg(toQString(id))
                            .toStdString();
        return std::unexpected(std::move(error));
    }
    const std::optional<std::uint32_t> serialNumber =
        key && std::holds_alternative<ProbeKey>(*key)
            ? std::optional<std::uint32_t>(std::get<ProbeKey>(*key).sn)
            : std::nullopt;
    if (serialNumber && sessionForProbe(*serialNumber) != nullptr) {
        Error error = makeError(
            ErrorCode::ProbeOpenFailed,
            std::format("J-Link {} is used by another session of this process", *serialNumber));
        error.message = QCoreApplication::translate(
                            "hio::device", "The J-Link %1 is already used by this application.")
                            .arg(*serialNumber)
                            .toStdString();
        return std::unexpected(std::move(error));
    }
    return {};
}

void SessionRegistry::add(ConsoleSession* session) { m_sessions.push_back(session); }

void SessionRegistry::remove(ConsoleSession* session) noexcept { std::erase(m_sessions, session); }

} // namespace hio::device
