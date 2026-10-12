#pragma once

// Probe enumeration with a separate library copy (ADR-0030): one-shot (enumerateProbes, a
// short-lived copy) or on a kept copy (ProbeEnumerator, ADR-0093: the GUI probe scanner lists every
// 2 s without copying and loading the library each time).
//
// Called synchronously on a caller-owned thread (CLI main thread; the GUI uses a worker, M3).
// Knows nothing about cross-process locks; libs/device adds the lock owner (ProbeLock, ADR-0058).

#include "hio/core/result.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/types.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace hio::jlink {

struct ProbeEnumeration {
    std::uint32_t libraryVersion = 0; // read from the loaded library
    std::vector<ProbeInfo> probes;    // in the library's order
};

class JLinkLibrary;

// ADR-0093: one library copy (its own, never shared with a session; ADR-0028/0030 rules unchanged)
// loaded once and used for any number of enumerations. Owned and movable; not thread-safe - use it
// from one thread only (the copy's vendor calls happen there). The destructor unloads the copy and
// removes its directory. When the library location changes the caller drops the enumerator and
// loads a new one (location() tells which library it holds).
class ProbeEnumerator {
public:
    // Copies and loads the library. Errors: those of JLinkLibrary::loadCopy.
    [[nodiscard]] static Result<ProbeEnumerator> load(const LibraryLocation& location,
                                                      const std::filesystem::path& instanceRoot);

    ProbeEnumerator(ProbeEnumerator&& other) noexcept;
    ProbeEnumerator& operator=(ProbeEnumerator&& other) noexcept;
    ProbeEnumerator(const ProbeEnumerator&) = delete;
    ProbeEnumerator& operator=(const ProbeEnumerator&) = delete;
    ~ProbeEnumerator();

    // USB and network probes in the library's order (bounded as in JLinkLibrary::listProbes).
    // Errors: probe.open_failed when the library cannot list probes.
    [[nodiscard]] Result<std::vector<ProbeInfo>> enumerate();
    [[nodiscard]] std::uint32_t libraryVersion() const;
    [[nodiscard]] const LibraryLocation& location() const noexcept { return m_location; }

private:
    ProbeEnumerator(LibraryLocation location, std::unique_ptr<JLinkLibrary> library);

    LibraryLocation m_location;
    std::unique_ptr<JLinkLibrary> m_library;
};

// A one-shot ProbeEnumerator (copy, load, list, unload).
// Errors: those of JLinkLibrary::loadCopy, probe.open_failed when the library cannot list probes.
[[nodiscard]] Result<ProbeEnumeration> enumerateProbes(const LibraryLocation& location,
                                                       const std::filesystem::path& instanceRoot);

// enumerateProbes() with JLinkLibrary::defaultInstanceRoot(), probes only.
[[nodiscard]] Result<std::vector<ProbeInfo>> listProbes(const LibraryLocation& location);

} // namespace hio::jlink
