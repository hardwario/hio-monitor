#pragma once

// Short-lived probe operations for `hio jlink info` and `hio jlink update-firmware`
// (plan/features/0002-m1-jlink-rtt.md). They run synchronously on the caller's thread with their
// own library copy (ADR-0030) and hold the probe's ProbeLock (ADR-0058) while they work. Without a
// serial number the first probe that is not locked is used.

#include "hio/core/result.h"
#include "hio/device/session_registry.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/types.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace hio::device {

struct ProbeDetails {
    std::filesystem::path libraryPath;
    std::uint32_t libraryVersion = 0;
    jlink::ProbeInfo probe;      // firmware = the probe's firmware string
    std::string libraryFirmware; // the firmware bundled in the library ("" when unknown)
    jlink::FirmwareStatus firmwareStatus = jlink::FirmwareStatus::Current;
    std::optional<int> targetMillivolts; // nullopt when the probe does not report it
};

// Errors: those of loading the library and opening the probe (probe.not_found,
// probe.open_failed, probe.in_use_by_other_process, ...).
[[nodiscard]] Result<ProbeDetails> inspectProbe(const SessionRegistry& registry,
                                                const jlink::LibraryLocation& location,
                                                std::optional<std::uint32_t> serialNumber);

struct FirmwareUpdate {
    std::uint32_t serialNumber = 0;
    jlink::FirmwareStatus before = jlink::FirmwareStatus::Current; // Current = nothing was done
};

// Updates (outdated) or synchronizes (newer) the probe firmware with the library's.
[[nodiscard]] Result<FirmwareUpdate> updateProbeFirmware(const SessionRegistry& registry,
                                                         const jlink::LibraryLocation& location,
                                                         std::optional<std::uint32_t> serialNumber);

} // namespace hio::device
