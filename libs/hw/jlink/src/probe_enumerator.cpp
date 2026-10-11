#include "hio/hw/jlink/probe_enumerator.h"

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/jlink_library.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/types.h"

#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace hio::jlink {

Result<ProbeEnumeration> enumerateProbes(const LibraryLocation& location,
                                         const std::filesystem::path& instanceRoot) {
    auto library = JLinkLibrary::loadCopy(location, instanceRoot);
    if (!library) {
        return std::unexpected(std::move(library).error());
    }
    auto probes = (*library)->listProbes(ProbeHosts::UsbAndIp);
    if (!probes) {
        std::string detail = std::format("JLINKARM_EMU_GetList returned {}", probes.error());
        const std::string libraryText = (*library)->takeErrorText();
        if (!libraryText.empty()) {
            detail += "; " + libraryText;
        }
        return std::unexpected(makeError(ErrorCode::ProbeOpenFailed, std::move(detail)));
    }
    (*library)->flushMessages();
    return ProbeEnumeration{.libraryVersion = (*library)->libraryVersion(),
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
