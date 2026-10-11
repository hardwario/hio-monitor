#pragma once

// Bounded file reading shared by libs/firmware (HEX files, cached catalog files).

#include "hio/core/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace hio::firmware::detail {

// Reads the whole file when it is at most `maxBytes` long. Errors: io.file_not_found,
// io.read_failed, firmware.invalid_image (larger than maxBytes; the size in `detail`).
[[nodiscard]] Result<std::vector<std::byte>> readFileBounded(const std::filesystem::path& path,
                                                             std::uint64_t maxBytes);

} // namespace hio::firmware::detail
