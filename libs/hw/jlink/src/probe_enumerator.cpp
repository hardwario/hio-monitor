#include "hio/hw/jlink/probe_enumerator.h"

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/jlink_library.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/types.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace hio::jlink {

Result<ProbeEnumerator> ProbeEnumerator::load(const LibraryLocation& location,
                                              const std::filesystem::path& instanceRoot) {
    auto library = JLinkLibrary::loadCopy(location, instanceRoot);
    if (!library) {
        return std::unexpected(std::move(library).error());
    }
    return ProbeEnumerator(location, std::move(*library));
}

ProbeEnumerator::ProbeEnumerator(LibraryLocation location, std::unique_ptr<JLinkLibrary> library)
    : m_location(std::move(location)), m_library(std::move(library)) {}

ProbeEnumerator::ProbeEnumerator(ProbeEnumerator&& other) noexcept = default;
ProbeEnumerator& ProbeEnumerator::operator=(ProbeEnumerator&& other) noexcept = default;
ProbeEnumerator::~ProbeEnumerator() = default;

Result<std::vector<ProbeInfo>> ProbeEnumerator::enumerate() {
    if (!m_library) {
        return std::unexpected(makeError(ErrorCode::InternalUnexpected, "moved-from enumerator"));
    }
    auto probes = m_library->listProbes(ProbeHosts::UsbAndIp);
    if (!probes) {
        std::string detail = std::format("JLINKARM_EMU_GetList returned {}", probes.error());
        const std::string libraryText = m_library->takeErrorText();
        if (!libraryText.empty()) {
            detail += "; " + libraryText;
        }
        return std::unexpected(makeError(ErrorCode::ProbeOpenFailed, std::move(detail)));
    }
    m_library->flushMessages();
    return std::move(*probes);
}

std::uint32_t ProbeEnumerator::libraryVersion() const {
    return m_library ? m_library->libraryVersion() : 0;
}

Result<ProbeEnumeration> enumerateProbes(const LibraryLocation& location,
                                         const std::filesystem::path& instanceRoot) {
    auto enumerator = ProbeEnumerator::load(location, instanceRoot);
    if (!enumerator) {
        return std::unexpected(std::move(enumerator).error());
    }
    auto probes = enumerator->enumerate();
    if (!probes) {
        return std::unexpected(std::move(probes).error());
    }
    return ProbeEnumeration{.libraryVersion = enumerator->libraryVersion(),
                            .probes = std::move(*probes)};
}

Result<std::vector<ProbeInfo>> listProbes(const LibraryLocation& location) {
    auto enumeration = enumerateProbes(location, JLinkLibrary::defaultInstanceRoot());
    if (!enumeration) {
        return std::unexpected(std::move(enumeration).error());
    }
    return std::move(enumeration->probes);
}

} // namespace hio::jlink
