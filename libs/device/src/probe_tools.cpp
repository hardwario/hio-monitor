#include "hio/device/probe_tools.h"

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/probe_lock.h"
#include "hio/device/session_registry.h"
#include "hio/hw/jlink/clock.h"
#include "hio/hw/jlink/jlink_library.h"
#include "hio/hw/jlink/jlink_probe.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/types.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace hio::device {

namespace {

// A library copy with one open, locked probe.
struct OpenProbe {
    std::unique_ptr<jlink::JLinkLibrary> library;
    std::unique_ptr<jlink::JLinkProbe> probe; // uses *library
    std::optional<ProbeLock> lock;
    jlink::ProbeInfo info;

    OpenProbe() = default;
    OpenProbe(const OpenProbe&) = delete;
    OpenProbe& operator=(const OpenProbe&) = delete;
    OpenProbe(OpenProbe&&) noexcept = default;
    OpenProbe& operator=(OpenProbe&&) noexcept = default;
    ~OpenProbe() {
        if (probe) {
            probe->close();
        }
        probe.reset(); // before the library it uses
    }
};

// The first listed probe that no session of this process uses and that is not locked; its lock.
Result<ProbeLock> lockFirstFree(const SessionRegistry& registry, jlink::JLinkProbe& probe) {
    auto probes = probe.list();
    if (!probes) {
        return std::unexpected(std::move(probes).error());
    }
    std::optional<Error> lockError;
    for (const jlink::ProbeInfo& candidate : *probes) {
        if (registry.sessionForProbe(candidate.serialNumber) != nullptr) {
            continue;
        }
        auto lock = ProbeLock::acquire(candidate.serialNumber, registry.options().lockDirectory);
        if (lock) {
            return lock;
        }
        if (!lockError) {
            lockError = std::move(lock).error();
        }
    }
    if (lockError) {
        return std::unexpected(std::move(*lockError));
    }
    return fail(ErrorCode::ProbeNotFound);
}

Result<OpenProbe> openExclusive(const SessionRegistry& registry,
                                const jlink::LibraryLocation& location,
                                std::optional<std::uint32_t> serialNumber) {
    if (serialNumber) {
        if (auto available = registry.checkAvailable(serialNumber);
            !available && available.error().code == ErrorCode::ProbeOpenFailed) {
            return std::unexpected(std::move(available).error());
        }
    }
    auto library = jlink::JLinkLibrary::loadCopy(location, registry.options().instanceRoot);
    if (!library) {
        return std::unexpected(std::move(library).error());
    }
    OpenProbe open;
    open.library = std::move(*library);
    open.probe = std::make_unique<jlink::JLinkProbe>(*open.library, jlink::SystemClock::instance());
    auto lock = serialNumber ? ProbeLock::acquire(*serialNumber, registry.options().lockDirectory)
                             : lockFirstFree(registry, *open.probe);
    if (!lock) {
        return std::unexpected(std::move(lock).error());
    }
    const std::uint32_t chosen = lock->serialNumber();
    open.lock = std::move(*lock);
    auto info = open.probe->open(chosen);
    if (!info) {
        return std::unexpected(std::move(info).error());
    }
    open.info = std::move(*info);
    return open;
}

} // namespace

Result<ProbeDetails> inspectProbe(const SessionRegistry& registry,
                                  const jlink::LibraryLocation& location,
                                  std::optional<std::uint32_t> serialNumber) {
    auto open = openExclusive(registry, location, serialNumber);
    if (!open) {
        return std::unexpected(std::move(open).error());
    }
    ProbeDetails details;
    details.libraryPath = location.path;
    details.libraryVersion = open->library->libraryVersion();
    details.probe = open->info;
    details.firmwareStatus = open->probe->firmwareStatus();
    if (auto embedded =
            open->library->embeddedFirmwareString(jlink::firmwareId(open->info.firmware))) {
        details.libraryFirmware = std::move(*embedded);
    }
    if (auto status = open->probe->hardwareStatus()) {
        details.targetMillivolts = status->targetMillivolts;
    }
    open->library->flushMessages();
    return details;
}

Result<FirmwareUpdate> updateProbeFirmware(const SessionRegistry& registry,
                                           const jlink::LibraryLocation& location,
                                           std::optional<std::uint32_t> serialNumber) {
    // Opened normally: the status "before" comes from a Normal open (review R24); updateFirmware()
    // reopens in FirmwareUpdate mode only when needed, then normally to verify (R11).
    auto open = openExclusive(registry, location, serialNumber);
    if (!open) {
        return std::unexpected(std::move(open).error());
    }
    auto before = open->probe->updateFirmware();
    if (!before) {
        return std::unexpected(std::move(before).error());
    }
    return FirmwareUpdate{.serialNumber = open->info.serialNumber, .before = *before};
}

} // namespace hio::device
