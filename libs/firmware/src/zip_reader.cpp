// ZipReader on miniz (ADR-0041). The only file that calls the miniz C API.

#include "hio/firmware/zip_reader.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"

#include <QByteArrayView>
#include <QFile>
#include <QIODevice>
#include <QSaveFile>
#include <QString>
#include <QtTypes>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <miniz.h>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace hio::firmware {

namespace {

namespace fs = std::filesystem;

Error invalidZip(std::string detail) {
    return makeError(ErrorCode::FirmwareInvalidImage, std::move(detail));
}

// miniz read callback: reads from the QFile in `opaque`. Never throws.
std::size_t readFromFile(void* opaque, mz_uint64 offset, void* buffer, std::size_t size) noexcept {
    auto* file = static_cast<QFile*>(opaque);
    if (offset > static_cast<mz_uint64>(std::numeric_limits<qint64>::max()) ||
        size > static_cast<std::size_t>(std::numeric_limits<qint64>::max()) ||
        !file->seek(static_cast<qint64>(offset))) {
        return 0;
    }
    const qint64 read = file->read(static_cast<char*>(buffer), static_cast<qint64>(size));
    return read < 0 ? 0 : static_cast<std::size_t>(read);
}

// Extraction sink: collects at most `limit` bytes; more makes the extraction fail, and so does a
// requested stop (review F2).
struct ByteSink {
    std::vector<std::byte> bytes;
    std::uint64_t limit = 0;
    std::stop_token stop;
    bool overflow = false;
    bool allocationFailed = false;
    bool stopped = false;
};

std::size_t writeToSink(void* opaque, mz_uint64 /*offset*/, const void* data,
                        std::size_t size) noexcept {
    auto* sink = static_cast<ByteSink*>(opaque);
    if (sink->stop.stop_requested()) {
        sink->stopped = true;
        return 0; // stops the extraction
    }
    if (sink->bytes.size() + size > sink->limit) {
        sink->overflow = true;
        return 0; // stops the extraction
    }
    try {
        const auto chunk = std::span(static_cast<const std::byte*>(data), size);
        sink->bytes.insert(sink->bytes.end(), chunk.begin(), chunk.end());
    } catch (...) {
        sink->allocationFailed = true;
        return 0;
    }
    return size;
}

bool hasDriveLetter(std::string_view name) {
    return name.size() >= 2 && name[1] == ':' &&
           ((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z'));
}

} // namespace

bool isUnsafeEntryName(std::string_view name) noexcept {
    if (name.empty() || name.front() == '/' || name.front() == '\\' || hasDriveLetter(name)) {
        return true;
    }
    // Any ".." path component, with either separator.
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t end = name.find_first_of("/\\", start);
        const std::string_view part = name.substr(
            start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (part == "..") {
            return true;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return name.find('\0') != std::string_view::npos;
}

struct ZipReader::Impl {
    fs::path path;
    ZipLimits limits;
    QFile file;
    mz_zip_archive archive{};
    bool initialized = false;
    std::vector<ZipEntry> entries;

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() {
        if (initialized) {
            mz_zip_reader_end(&archive);
        }
    }

    [[nodiscard]] std::string lastError() {
        return mz_zip_get_error_string(mz_zip_get_last_error(&archive));
    }

    [[nodiscard]] Result<mz_uint> indexOf(std::string_view name) const {
        const auto it = std::ranges::find(entries, name, &ZipEntry::name);
        if (it == entries.end()) {
            return std::unexpected(
                invalidZip(std::format("{}: no entry \"{}\"", toUtf8(path), name)));
        }
        return static_cast<mz_uint>(it - entries.begin());
    }
};

Result<ZipReader> ZipReader::open(const fs::path& path, ZipLimits limits) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        return std::unexpected(makeError(ErrorCode::IoFileNotFound, toUtf8(path)));
    }
    auto impl = std::make_unique<Impl>();
    impl->path = path;
    impl->limits = limits;
    impl->file.setFileName(toQString(toUtf8(path)));
    if (!impl->file.open(QIODevice::ReadOnly)) {
        return std::unexpected(makeError(
            ErrorCode::IoReadFailed, toUtf8(path) + ": " + impl->file.errorString().toStdString()));
    }
    mz_zip_zero_struct(&impl->archive);
    impl->archive.m_pRead = &readFromFile;
    impl->archive.m_pIO_opaque = &impl->file;
    if (mz_zip_reader_init(&impl->archive, static_cast<mz_uint64>(impl->file.size()), 0) ==
        MZ_FALSE) {
        return std::unexpected(
            invalidZip(std::format("{}: not a zip archive ({})", toUtf8(path), impl->lastError())));
    }
    impl->initialized = true;

    const mz_uint count = mz_zip_reader_get_num_files(&impl->archive);
    if (count > limits.maxEntries) {
        return std::unexpected(invalidZip(std::format("{}: {} entries, the limit is {}",
                                                      toUtf8(path), count, limits.maxEntries)));
    }
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (mz_zip_reader_file_stat(&impl->archive, i, &stat) == MZ_FALSE) {
            return std::unexpected(invalidZip(
                std::format("{}: entry {} unreadable ({})", toUtf8(path), i, impl->lastError())));
        }
        const std::span<const char> storedName(stat.m_filename);
        ZipEntry entry{.name = std::string(storedName.begin(), std::ranges::find(storedName, '\0')),
                       .size = stat.m_uncomp_size,
                       .compressedSize = stat.m_comp_size,
                       .directory = stat.m_is_directory != MZ_FALSE};
        if (isUnsafeEntryName(entry.name)) {
            return std::unexpected(
                invalidZip(std::format("{}: unsafe entry name \"{}\"", toUtf8(path), entry.name)));
        }
        if (stat.m_is_encrypted != MZ_FALSE || stat.m_is_supported == MZ_FALSE) {
            return std::unexpected(invalidZip(std::format(
                "{}: entry \"{}\" is encrypted or unsupported", toUtf8(path), entry.name)));
        }
        if (entry.size > limits.maxEntryBytes) {
            return std::unexpected(invalidZip(std::format("{}: entry \"{}\" has {} bytes, the "
                                                          "limit is {}",
                                                          toUtf8(path), entry.name, entry.size,
                                                          limits.maxEntryBytes)));
        }
        const std::uint64_t compressed = std::max<std::uint64_t>(entry.compressedSize, 1);
        if (entry.size / compressed > limits.maxCompressionRatio) {
            return std::unexpected(
                invalidZip(std::format("{}: entry \"{}\" has a compression ratio above {}",
                                       toUtf8(path), entry.name, limits.maxCompressionRatio)));
        }
        impl->entries.push_back(std::move(entry));
    }
    return ZipReader(std::move(impl));
}

ZipReader::ZipReader(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
ZipReader::ZipReader(ZipReader&& other) noexcept = default;
ZipReader& ZipReader::operator=(ZipReader&& other) noexcept = default;
ZipReader::~ZipReader() = default;

const std::vector<ZipEntry>& ZipReader::entries() const noexcept { return m_impl->entries; }

Result<std::vector<std::byte>> ZipReader::extract(std::string_view name, std::uint64_t maxBytes,
                                                  const std::stop_token& stop) {
    auto index = m_impl->indexOf(name);
    if (!index) {
        return std::unexpected(std::move(index).error());
    }
    const auto cancelled = [&] {
        return std::unexpected(
            makeError(ErrorCode::Cancelled,
                      std::format("{}: extraction of \"{}\" stopped", toUtf8(m_impl->path), name)));
    };
    if (stop.stop_requested()) {
        return cancelled();
    }
    ByteSink sink;
    sink.limit = std::min(maxBytes, m_impl->limits.maxEntryBytes);
    sink.stop = stop;
    const mz_bool ok =
        mz_zip_reader_extract_to_callback(&m_impl->archive, *index, &writeToSink, &sink, 0);
    if (sink.stopped) {
        return cancelled();
    }
    if (sink.overflow) {
        return std::unexpected(invalidZip(std::format("{}: entry \"{}\" is larger than {} bytes",
                                                      toUtf8(m_impl->path), name, sink.limit)));
    }
    if (ok == MZ_FALSE || sink.allocationFailed) {
        return std::unexpected(
            invalidZip(std::format("{}: entry \"{}\" cannot be extracted ({})",
                                   toUtf8(m_impl->path), name, m_impl->lastError())));
    }
    return std::move(sink.bytes);
}

Result<fs::path> ZipReader::extractTo(std::string_view name, const fs::path& directory,
                                      std::uint64_t maxBytes, const std::stop_token& stop) {
    auto bytes = extract(name, maxBytes, stop);
    if (!bytes) {
        return std::unexpected(std::move(bytes).error());
    }
    fs::path target = directory / fs::path(toQString(name).toStdU16String());
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    QSaveFile file(toQString(toUtf8(target)));
    if (!file.open(QIODevice::WriteOnly)) {
        return std::unexpected(makeError(ErrorCode::IoWriteFailed,
                                         toUtf8(target) + ": " + file.errorString().toStdString()));
    }
    const auto chars = std::span(*bytes);
    const qint64 written =
        file.write(QByteArrayView(chars.data(), static_cast<qsizetype>(chars.size())).data(),
                   static_cast<qint64>(chars.size()));
    if (std::cmp_not_equal(written, chars.size()) || !file.commit()) {
        return std::unexpected(makeError(ErrorCode::IoWriteFailed,
                                         toUtf8(target) + ": " + file.errorString().toStdString()));
    }
    return target;
}

} // namespace hio::firmware
