#pragma once

// Firmware IDs and the bounded cache of catalog downloads (ADR-0037, plan/features/0003).
//
// Cache: <directory>/<id>.hex, written with QSaveFile only after a complete download that parsed
// as Intel HEX. A hit is parsed again (a corrupt entry is deleted and counts as a miss) and
// touched (mtime = now). LRU by mtime: after each insert the oldest files are deleted until at
// most maxFiles files and maxBytes bytes remain. Directory errors never fail a download: they are
// returned as warnings (io.write_failed) and the download continues in memory.
//
// Plan 0021 (ADR-0165): designated firmware is cached as <directory>/lte-<sha256>.zip, written
// only after its SHA-256 was verified; every hit is hashed again (a mismatch deletes the entry and
// counts as a miss). These entries count against the same limits (one LRU over both kinds).
//
// Thread safety: none; used on the owner thread (FirmwareCatalog).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/firmware/designated_firmware.h"
#include "hio/firmware/firmware_image.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::firmware {

// Exactly 32 hexadecimal digits, normalized to lowercase.
class FirmwareId {
public:
    // Errors: usage.invalid_argument.
    [[nodiscard]] static Result<FirmwareId> parse(std::string_view text);
    // True when `text` has the form of an ID (used to tell an ID from a missing file).
    [[nodiscard]] static bool looksLikeId(std::string_view text) noexcept;

    [[nodiscard]] const std::string& value() const noexcept { return m_value; }
    friend bool operator==(const FirmwareId&, const FirmwareId&) = default;

private:
    explicit FirmwareId(std::string value) : m_value(std::move(value)) {}
    std::string m_value;
};

struct CacheLimits {
    std::size_t maxFiles = 100;
    std::uint64_t maxBytes = 500ULL * 1024 * 1024;
};

struct CacheHit {
    FirmwareImage image;
    std::uint64_t fileBytes = 0;
    std::filesystem::path file;
    Sha256 fileSha256{}; // of the HEX text (plan 0021: compared with the catalog page)
};

// Upper bound of a designated firmware file (= the catalog download cap, ADR-0037).
inline constexpr std::uint64_t maxDesignatedFileBytes = 32ULL * 1024 * 1024;

class FirmwareCache {
public:
    explicit FirmwareCache(std::filesystem::path directory, CacheLimits limits = {});

    // AppLocalDataLocation/firmware-cache (requires initApplicationIdentity()).
    [[nodiscard]] static std::filesystem::path defaultDirectory();

    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return m_directory; }
    [[nodiscard]] std::filesystem::path fileFor(const FirmwareId& id) const;

    // A valid entry (parsed again, touched), or nullopt (missing or corrupt; a corrupt one is
    // deleted).
    [[nodiscard]] std::optional<CacheHit> lookup(const FirmwareId& id) const;
    // Stores the downloaded HEX text atomically, then enforces the limits (the new file is kept).
    // Errors: io.write_failed (the caller treats it as a warning).
    [[nodiscard]] Status store(const FirmwareId& id, std::span<const std::byte> hexText);
    // Deletes the oldest entries beyond the limits, never `keep`. Returns the number deleted.
    std::size_t evict(const std::optional<FirmwareId>& keep = std::nullopt);

    // ---- designated firmware (plan 0021, ADR-0165) ----
    [[nodiscard]] std::filesystem::path designatedFileFor(const Sha256& sha256) const;
    // The cached file's bytes when they still have `sha256` (touched); nullopt when missing or
    // damaged (a damaged entry is deleted). The bytes are the verified ones (review F5).
    [[nodiscard]] std::optional<std::vector<std::byte>>
    lookupDesignated(const Sha256& sha256) const;
    // Stores verified bytes atomically, then enforces the limits (the new file is kept).
    // The caller has checked sha256Of(bytes) == sha256. Errors: io.write_failed.
    [[nodiscard]] Status storeDesignated(const Sha256& sha256, std::span<const std::byte> bytes);

private:
    std::size_t evictKeeping(const std::filesystem::path& kept);
    [[nodiscard]] Status storeFile(const std::filesystem::path& file,
                                   std::span<const std::byte> bytes);

    std::filesystem::path m_directory;
    CacheLimits m_limits;
};

} // namespace hio::firmware
