#include "hio/device/console_file_sink.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/probe_lock.h"

#include <QDateTime>
#include <QFile>
#include <QIODevice>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QString>
#include <QTimeZone>
#include <QtLogging>
#include <QtTypes>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace hio::device {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t headerWidth = 80;

Error writeFailed(const fs::path& file, const QString& reason) {
    return makeError(ErrorCode::IoWriteFailed, toUtf8(file) + ": " + reason.toStdString());
}

QDateTime toDateTime(std::chrono::system_clock::time_point time) {
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
    return QDateTime::fromMSecsSinceEpoch(ms);
}

std::string timestamp(std::chrono::system_clock::time_point time) {
    return toDateTime(time).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")).toStdString();
}

std::string headerBlock(std::string_view text) {
    const std::string stars(headerWidth, '*');
    const std::size_t inner = headerWidth - 6;
    std::string centered(text.substr(0, inner));
    const std::size_t padding = inner - centered.size();
    centered = std::string(padding / 2, ' ') + centered + std::string(padding - (padding / 2), ' ');
    return stars + "\n***" + centered + "***\n" + stars + "\n";
}

fs::path partFile(const fs::path& base, int part) {
    if (part <= 1) {
        return base;
    }
    fs::path file = base;
    file.replace_extension(std::format(".part{}.log", part));
    return file;
}

struct FileInfo {
    fs::path path;
    fs::file_time_type modified;
    std::uintmax_t size = 0;
};

// The console files this process has open, in every session (review R45): the retention never
// deletes them. Owner: ConsoleFileSink (openCurrent() adds, closeFile() removes); guarded by the
// mutex (sinks run on different session workers). Bounded by the open sinks (8 sessions).
class OpenFiles {
public:
    void add(const fs::path& file) {
        const std::scoped_lock lock(m_mutex);
        m_files.insert(key(file));
    }
    void remove(const fs::path& file) {
        const std::scoped_lock lock(m_mutex);
        if (const auto it = m_files.find(key(file)); it != m_files.end()) {
            m_files.erase(it); // one instance: two sinks may append to the same explicit file
        }
    }
    [[nodiscard]] bool contains(const fs::path& file) const {
        const std::scoped_lock lock(m_mutex);
        return m_files.contains(key(file));
    }

private:
    static fs::path key(const fs::path& file) {
        std::error_code ec;
        fs::path canonical = fs::weakly_canonical(file, ec);
        return ec ? fs::absolute(file, ec).lexically_normal() : canonical;
    }

    mutable std::mutex m_mutex;
    std::multiset<fs::path> m_files;
};

OpenFiles& openFiles() {
    static OpenFiles files;
    return files;
}

// The probe serial number of a default console file name,
// <profile>-<target>-YYYYMMDD-HHMMSS-<sn>.log or <...>-<sn>.partN.log; nullopt for other names.
std::optional<std::uint32_t> serialNumberOf(const fs::path& file) {
    std::string stem = file.stem().string(); // without ".log"
    if (const auto part = stem.rfind(".part"); part != std::string::npos) {
        stem.resize(part);
    }
    const auto dash = stem.rfind('-');
    if (dash == std::string::npos || dash + 1 >= stem.size()) {
        return std::nullopt;
    }
    const std::string_view digits = std::string_view(stem).substr(dash + 1);
    constexpr std::size_t maxDigits = 10; // std::uint32_t
    if (digits.size() > maxDigits ||
        !std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char c : digits) {
        value = (value * 10) + static_cast<std::uint64_t>(c - '0');
    }
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

// Coarse file-system timestamps (FAT: 2 s): a file written within this time before its probe's
// lock was taken still counts as the lock owner's file.
constexpr std::chrono::seconds lockTimeTolerance{2};

} // namespace

std::string_view consoleFilePrefix(const ConsoleLine& line) noexcept {
    if (line.dir == Direction::In) {
        return " < ";
    }
    if (line.dir == Direction::Marker) {
        return " @ ";
    }
    return line.channel == Channel::Log ? " # " : " > ";
}

fs::path ConsoleFileSink::defaultDirectory() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return fs::path(base.toStdU16String()) / "console";
}

Result<std::unique_ptr<ConsoleFileSink>>
ConsoleFileSink::open(const ConsoleFileConfig& config, std::uint32_t probeSerialNumber,
                      std::string_view headerText, std::chrono::system_clock::time_point now) {
    fs::path base;
    if (config.explicitFile) {
        base = *config.explicitFile;
    } else {
        const QString stamp =
            toDateTime(now).toTimeZone(QTimeZone::UTC).toString(QStringLiteral("yyyyMMdd-HHmmss"));
        base = config.directory / std::format("{}-{}-{}-{}.log", config.profileId, config.targetId,
                                              stamp.toStdString(), probeSerialNumber);
    }
    auto sink = std::make_unique<ConsoleFileSink>(Passkey{}, config, std::move(base));
    if (auto status = sink->openCurrent(); !status) {
        return std::unexpected(std::move(status).error());
    }
    sink->m_buffer += headerBlock(headerText);
    if (auto status = sink->flush(); !status) {
        return std::unexpected(std::move(status).error());
    }
    if (!config.explicitFile) {
        sink->prune();
    }
    return sink;
}

ConsoleFileSink::ConsoleFileSink(Passkey /*key*/, ConsoleFileConfig config, fs::path baseFile)
    : m_config(std::move(config)), m_base(std::move(baseFile)), m_current(m_base) {}

ConsoleFileSink::~ConsoleFileSink() {
    if (m_file && !m_buffer.empty()) {
        if (const Status status = flush(); !status) {
            qCWarning(log::lcDevice).noquote() << toQString(status.error().detail);
        }
    }
    closeFile();
}

Status ConsoleFileSink::openCurrent() {
    std::error_code ec;
    if (m_current.has_parent_path()) {
        fs::create_directories(m_current.parent_path(), ec);
    }
    m_file = std::make_unique<QFile>(toQString(toUtf8(m_current)));
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Append)) {
        const QString reason = m_file->errorString();
        m_file = nullptr;
        return std::unexpected(writeFailed(m_current, reason));
    }
    m_written = static_cast<std::uintmax_t>(m_file->size());
    openFiles().add(m_current);
    qCInfo(log::lcDevice).noquote() << "Console file:" << toQString(toUtf8(m_current));
    return {};
}

void ConsoleFileSink::closeFile() {
    if (m_file) {
        m_file = nullptr; // closes
        openFiles().remove(m_current);
    }
}

Status ConsoleFileSink::rotateIfNeeded(std::size_t incoming) {
    const std::uintmax_t limit = m_config.explicitFile ? maxExplicitFileBytes : maxDefaultFileBytes;
    if (m_written == 0 || m_written + incoming <= limit) {
        return {};
    }
    if (m_config.explicitFile && m_rotationFailed) {
        // After a failed rotation the file is not rotated again; it keeps growing up to twice the
        // limit, then it is not written any more (the session drops the sink; review R26).
        if (m_written + incoming > maxExplicitFileBytesAfterFailedRotation) {
            closeFile();
            return std::unexpected(
                writeFailed(m_current, QStringLiteral("the file exceeds %1 bytes after a failed "
                                                      "rotation; it is not written any more")
                                           .arg(maxExplicitFileBytesAfterFailedRotation)));
        }
        return {};
    }
    closeFile();
    if (m_config.explicitFile) {
        fs::path backup = m_base;
        backup += ".1";
        std::error_code ec;
        fs::remove(backup, ec);
        fs::rename(m_base, backup, ec);
        if (ec) {
            // Not retried for the rest of the session; one warning (review R26).
            m_rotationFailed = true;
            qCWarning(log::lcDevice).noquote()
                << "Console file rotation failed:" << toQString(ec.message());
            m_warning = writeFailed(
                backup, QStringLiteral("cannot rotate the console file (%1); it keeps growing")
                            .arg(QString::fromStdString(ec.message())));
        }
    } else {
        ++m_part;
        m_current = partFile(m_base, m_part);
    }
    auto status = openCurrent();
    if (status && !m_config.explicitFile) {
        prune();
    }
    return status;
}

std::optional<Error> ConsoleFileSink::takeWarning() {
    return std::exchange(m_warning, std::nullopt);
}

Status ConsoleFileSink::append(std::string_view prefix, std::string_view text,
                               std::chrono::system_clock::time_point time) {
    m_buffer += timestamp(time);
    m_buffer += prefix;
    m_buffer += text;
    m_buffer += '\n';
    if (m_buffer.size() >= maxBufferedBytes) {
        return flush();
    }
    return {};
}

Status ConsoleFileSink::write(const ConsoleLine& line) {
    return append(consoleFilePrefix(line), line.text, line.time);
}

Status ConsoleFileSink::writeConnection(std::string_view text,
                                        std::chrono::system_clock::time_point time) {
    return append(" @ ", text, time);
}

Status ConsoleFileSink::flush() {
    if (m_buffer.empty()) {
        return {};
    }
    if (auto status = rotateIfNeeded(m_buffer.size()); !status) {
        m_buffer.clear();
        return status;
    }
    if (!m_file) {
        m_buffer.clear();
        return std::unexpected(writeFailed(m_current, QStringLiteral("not open")));
    }
    const auto written = m_file->write(m_buffer.data(), static_cast<qint64>(m_buffer.size()));
    const bool flushed = m_file->flush();
    m_buffer.clear();
    if (written < 0 || !flushed) {
        return std::unexpected(writeFailed(m_current, m_file->errorString()));
    }
    m_written += static_cast<std::uintmax_t>(written);
    return {};
}

void ConsoleFileSink::prune() const {
    std::vector<FileInfo> files;
    std::error_code ec;
    for (fs::directory_iterator it(m_config.directory, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code entryError;
        if (!it->is_regular_file(entryError) || it->path().extension() != ".log") {
            continue;
        }
        FileInfo info{.path = it->path(),
                      .modified = it->last_write_time(entryError),
                      .size = it->file_size(entryError)};
        if (!entryError) {
            files.push_back(std::move(info));
        }
    }
    std::ranges::sort(files, [](const FileInfo& a, const FileInfo& b) {
        return a.modified > b.modified; // newest first
    });
    // A file a live session may be writing is kept (review R45): one this process has open, or
    // one of a probe with a live lock owner (this or another process, ProbeLock::owner() rules),
    // written since the owner took the lock (earlier files are from finished sessions).
    const fs::path lockDirectory =
        m_config.lockDirectory.empty() ? ProbeLock::defaultDirectory() : m_config.lockDirectory;
    std::map<std::uint32_t, std::optional<ProbeLockOwner>> owners; // bounded by the files
    const auto isLive = [&](const FileInfo& file) {
        if (openFiles().contains(file.path)) {
            return true;
        }
        const std::optional<std::uint32_t> serialNumber = serialNumberOf(file.path);
        if (!serialNumber) {
            return false;
        }
        auto it = owners.find(*serialNumber);
        if (it == owners.end()) {
            it =
                owners.emplace(*serialNumber, ProbeLock::owner(*serialNumber, lockDirectory)).first;
        }
        const std::optional<ProbeLockOwner>& owner = it->second;
        return owner.has_value() &&
               (!owner->lockedAt || file.modified + lockTimeTolerance >= *owner->lockedAt);
    };
    std::size_t kept = 0;
    std::uintmax_t total = 0;
    bool deleting = false;
    for (const FileInfo& file : files) {
        std::error_code sameError;
        const bool current = fs::equivalent(file.path, m_current, sameError);
        if (!current && !isLive(file) &&
            (deleting || kept >= maxDefaultFiles || total + file.size > maxDefaultTotalBytes)) {
            deleting = true; // by age: everything older than a deleted file goes too
            std::error_code removeError;
            fs::remove(file.path, removeError);
            continue;
        }
        ++kept;
        total += file.size;
    }
}

} // namespace hio::device
