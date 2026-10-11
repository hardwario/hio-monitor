#include "hio/device/session_registry.h"

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/console_file_sink.h"
#include "hio/device/device_session.h"
#include "hio/device/probe_lock.h"
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
#include <utility>
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
    const auto it = std::ranges::find_if(m_sessions, [serialNumber](const DeviceSession* session) {
        return session->probe().serialNumber == serialNumber ||
               detail::SessionInternals::requestedSerial(*session) == serialNumber;
    });
    return it == m_sessions.end() ? nullptr : *it;
}

Status SessionRegistry::checkAvailable(std::optional<std::uint32_t> serialNumber) const {
    if (m_sessions.size() >= maxSessions) {
        return std::unexpected(
            makeError(ErrorCode::InternalSessionLimit,
                      std::format("{} of {} sessions are open", m_sessions.size(), maxSessions)));
    }
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

void SessionRegistry::add(DeviceSession* session) { m_sessions.push_back(session); }

void SessionRegistry::remove(DeviceSession* session) noexcept { std::erase(m_sessions, session); }

} // namespace hio::device
