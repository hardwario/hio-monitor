#include "hio/firmware/firmware_cache.h"

#include "file_io.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/firmware/designated_firmware.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/intel_hex.h"

#include <QByteArrayView>
#include <QCoreApplication>
#include <QIODevice>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QtLogging>
#include <QtTypes>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace hio::firmware {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t idLength = 32;
constexpr std::string_view cacheSuffix = ".hex";
constexpr std::string_view designatedPrefix = "lte-";
constexpr std::string_view designatedSuffix = ".zip";

// <id>.hex or lte-<sha256>.zip: the entries of this cache (QSaveFile temporaries and other files
// are never touched).
bool isCacheEntry(const fs::path& path) {
    const std::string stem = toUtf8(path.stem());
    const std::string extension = toUtf8(path.extension());
    if (extension == cacheSuffix) {
        return FirmwareId::looksLikeId(stem);
    }
    return extension == designatedSuffix && stem.starts_with(designatedPrefix) &&
           parseSha256(std::string_view(stem).substr(designatedPrefix.size())).has_value();
}

bool isHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

} // namespace

bool FirmwareId::looksLikeId(std::string_view text) noexcept {
    return text.size() == idLength && std::ranges::all_of(text, isHexDigit);
}

Result<FirmwareId> FirmwareId::parse(std::string_view text) {
    if (!looksLikeId(text)) {
        Error error = makeError(ErrorCode::UsageInvalidArgument, std::string(text));
        error.message = QCoreApplication::translate(
                            "hio::firmware", "A firmware ID has exactly 32 hexadecimal digits.")
                            .toStdString();
        return std::unexpected(std::move(error));
    }
    std::string value(text);
    std::ranges::transform(value, value.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return FirmwareId(std::move(value));
}

FirmwareCache::FirmwareCache(fs::path directory, CacheLimits limits)
    : m_directory(std::move(directory)), m_limits(limits) {}

fs::path FirmwareCache::defaultDirectory() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return fs::path(base.toStdU16String()) / "firmware-cache";
}

fs::path FirmwareCache::fileFor(const FirmwareId& id) const {
    return m_directory / (id.value() + std::string(cacheSuffix));
}

std::optional<CacheHit> FirmwareCache::lookup(const FirmwareId& id) const {
    const fs::path file = fileFor(id);
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        return std::nullopt;
    }
    auto bytes = detail::readFileBounded(file, maxHexFileBytes);
    auto image = bytes ? parseIntelHex(*bytes, "cache " + id.value())
                       : Result<FirmwareImage>(std::unexpected(bytes.error()));
    if (!image) {
        qCWarning(log::lcFirmware).noquote()
            << "Corrupt firmware cache entry deleted:" << toQString(toUtf8(file)) << "-"
            << toQString(image.error().detail);
        fs::remove(file, ec);
        return std::nullopt;
    }
    fs::last_write_time(file, fs::file_time_type::clock::now(), ec); // LRU touch
    qCInfo(log::lcFirmware).noquote() << "Firmware cache hit:" << toQString(toUtf8(file));
    const Sha256 digest = sha256Of(*bytes);
    return CacheHit{
        .image = std::move(*image), .fileBytes = bytes->size(), .file = file, .fileSha256 = digest};
}

Status FirmwareCache::store(const FirmwareId& id, std::span<const std::byte> hexText) {
    const fs::path file = fileFor(id);
    if (Status stored = storeFile(file, hexText); !stored) {
        return stored;
    }
    evict(id);
    return {};
}

fs::path FirmwareCache::designatedFileFor(const Sha256& sha256) const {
    return m_directory /
           (std::string(designatedPrefix) + toHex(sha256) + std::string(designatedSuffix));
}

std::optional<std::vector<std::byte>> FirmwareCache::lookupDesignated(const Sha256& sha256) const {
    const fs::path file = designatedFileFor(sha256);
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        return std::nullopt;
    }
    auto bytes = detail::readFileBounded(file, maxDesignatedFileBytes);
    if (!bytes || sha256Of(*bytes) != sha256) {
        qCWarning(log::lcFirmware).noquote()
            << "Damaged firmware cache entry deleted:" << toQString(toUtf8(file));
        fs::remove(file, ec);
        return std::nullopt;
    }
    fs::last_write_time(file, fs::file_time_type::clock::now(), ec); // LRU touch
    qCInfo(log::lcFirmware).noquote() << "Firmware cache hit:" << toQString(toUtf8(file));
    return std::move(*bytes);
}

Status FirmwareCache::storeDesignated(const Sha256& sha256, std::span<const std::byte> bytes) {
    const fs::path file = designatedFileFor(sha256);
    if (Status stored = storeFile(file, bytes); !stored) {
        return stored;
    }
    evictKeeping(file);
    return {};
}

Status FirmwareCache::storeFile(const fs::path& file, std::span<const std::byte> bytes) {
    std::error_code ec;
    fs::create_directories(m_directory, ec);
    if (ec) {
        return std::unexpected(
            makeError(ErrorCode::IoWriteFailed, toUtf8(m_directory) + ": " + ec.message()));
    }
    QSaveFile save(toQString(toUtf8(file)));
    if (!save.open(QIODevice::WriteOnly)) {
        return std::unexpected(makeError(ErrorCode::IoWriteFailed,
                                         toUtf8(file) + ": " + save.errorString().toStdString()));
    }
    const QByteArrayView view(bytes.data(), static_cast<qsizetype>(bytes.size()));
    if (save.write(view.data(), view.size()) != view.size() || !save.commit()) {
        return std::unexpected(makeError(ErrorCode::IoWriteFailed,
                                         toUtf8(file) + ": " + save.errorString().toStdString()));
    }
    qCInfo(log::lcFirmware).noquote() << "Firmware cached:" << toQString(toUtf8(file));
    return {};
}

std::size_t FirmwareCache::evict(const std::optional<FirmwareId>& keep) {
    return evictKeeping(keep ? fileFor(*keep) : fs::path());
}

std::size_t FirmwareCache::evictKeeping(const fs::path& kept) {
    struct Entry {
        fs::path path;
        fs::file_time_type modified;
        std::uintmax_t size = 0;
    };
    std::vector<Entry> entries;
    std::error_code ec;
    for (fs::directory_iterator it(m_directory, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryError;
        const fs::path& path = it->path();
        if (!it->is_regular_file(entryError) || !isCacheEntry(path)) {
            continue; // not ours (QSaveFile temporaries, other files)
        }
        entries.push_back(Entry{.path = path,
                                .modified = it->last_write_time(entryError),
                                .size = it->file_size(entryError)});
    }
    // Newest first; the oldest are deleted.
    std::ranges::sort(entries,
                      [](const Entry& a, const Entry& b) { return a.modified > b.modified; });
    std::size_t files = 0;
    std::uint64_t bytes = 0;
    std::size_t removed = 0;
    // The kept file always counts first.
    for (const Entry& entry : entries) {
        if (entry.path == kept) {
            ++files;
            bytes += entry.size;
        }
    }
    bool full = false; // strict LRU: once one entry is evicted, every older one is too
    for (const Entry& entry : entries) {
        if (entry.path == kept) {
            continue;
        }
        full = full || files + 1 > m_limits.maxFiles || bytes + entry.size > m_limits.maxBytes;
        if (!full) {
            ++files;
            bytes += entry.size;
            continue;
        }
        std::error_code removeError;
        if (fs::remove(entry.path, removeError)) {
            ++removed;
            qCInfo(log::lcFirmware).noquote()
                << "Firmware cache entry evicted:" << toQString(toUtf8(entry.path));
        }
    }
    return removed;
}

} // namespace hio::firmware
