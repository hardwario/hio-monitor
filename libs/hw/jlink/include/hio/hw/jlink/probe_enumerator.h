#pragma once

// Probe enumeration with a separate, short-lived library copy (ADR-0030).
//
// Called synchronously on a caller-owned thread (CLI main thread; the GUI uses a worker, M3).
// Knows nothing about cross-process locks; libs/device adds the lock owner (ProbeLock, ADR-0058).

#include "hio/core/result.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/types.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace hio::jlink {

struct ProbeEnumeration {
    std::uint32_t libraryVersion = 0; // read from the loaded library
    std::vector<ProbeInfo> probes;    // in the library's order
};

// Errors: those of JLinkLibrary::loadCopy, probe.open_failed when the library cannot list probes.
[[nodiscard]] Result<ProbeEnumeration> enumerateProbes(const LibraryLocation& location,
                                                       const std::filesystem::path& instanceRoot);

// enumerateProbes() with JLinkLibrary::defaultInstanceRoot(), probes only.
[[nodiscard]] Result<std::vector<ProbeInfo>> listProbes(const LibraryLocation& location);

} // namespace hio::jlink
