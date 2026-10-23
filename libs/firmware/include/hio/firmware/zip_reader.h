#pragma once

// ZipReader - read-only access to a zip archive (ADR-0041, miniz; plan/features/0003).
//
// The archive is read through a QFile (Unicode paths everywhere); miniz is used only in
// zip_reader.cpp. open() checks the whole central directory against the limits before anything
// is extracted: at most maxEntries entries, at most maxEntryBytes uncompressed per entry, a
// compression ratio of at most maxCompressionRatio, no encrypted entries and no entry names with
// "..", an absolute path or a drive letter (zip slip). Extraction counts the real bytes and stops
// at the limit, whatever the archive declares. Nested zips are plain entries (bytes).
// Errors: firmware.invalid_image with the reason in `detail`; io.* for the file itself.
//
// Thread safety: none (one owner thread). Move-only RAII.

#include "hio/core/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace hio::firmware {

struct ZipLimits {
    std::size_t maxEntries = 64;
    std::uint64_t maxEntryBytes = 128ULL * 1024 * 1024;
    std::uint64_t maxCompressionRatio = 200;
};

struct ZipEntry {
    std::string name; // as stored ('/' separators)
    std::uint64_t size = 0;
    std::uint64_t compressedSize = 0;
    bool directory = false;
};

class ZipReader {
public:
    struct Impl;

    [[nodiscard]] static Result<ZipReader> open(const std::filesystem::path& path,
                                                ZipLimits limits = {});

    ZipReader(ZipReader&& other) noexcept;
    ZipReader& operator=(ZipReader&& other) noexcept;
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;
    ~ZipReader();

    [[nodiscard]] const std::vector<ZipEntry>& entries() const noexcept;

    // The uncompressed bytes of entry `name` (at most min(maxBytes, maxEntryBytes)). `stop` is
    // checked for every decompressed chunk (review F2); a requested stop gives `cancelled`.
    [[nodiscard]] Result<std::vector<std::byte>>
    extract(std::string_view name, std::uint64_t maxBytes, const std::stop_token& stop = {});
    // Extracts entry `name` into `directory` (keeping its relative path, which open() validated)
    // and returns the written file. Errors also: io.write_failed, `cancelled` (as extract()).
    [[nodiscard]] Result<std::filesystem::path> extractTo(std::string_view name,
                                                          const std::filesystem::path& directory,
                                                          std::uint64_t maxBytes,
                                                          const std::stop_token& stop = {});

private:
    explicit ZipReader(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

// True for names that would leave the extraction directory: "..", absolute paths, drive letters,
// backslash-rooted names (exposed for tests).
[[nodiscard]] bool isUnsafeEntryName(std::string_view name) noexcept;

} // namespace hio::firmware
