#include "hwio/device/command_history.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/log.h"
#include "hwio/core/result.h"
#include "hwio/device/profile.h"
#include "owner_only.h"

#include <QByteArray>
#include <QFile>
#include <QFileDevice>
#include <QIODevice>
#include <QLockFile>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QtLogging>
#include <QtTypes>

#include <chrono>
#include <cstddef>
#include <deque>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace hwio::device {

namespace {

namespace fs = std::filesystem;

// clearAll() looks at no more entries of the history directory than this.
constexpr std::size_t maxDirectoryEntries = 10000;

// Appends with the rules of the history: consecutive duplicates collapsed, newest maxEntries kept.
void appendEntry(std::deque<std::string>& entries, std::string entry) {
    if (!entries.empty() && entries.back() == entry) {
        return;
    }
    entries.push_back(std::move(entry));
    while (entries.size() > CommandHistory::maxEntries) {
        entries.pop_front();
    }
}

bool isStorable(std::string_view entry) {
    return !entry.empty() && entry.size() <= CommandHistory::maxEntryBytes &&
           entry.find_first_of("\r\n") == std::string_view::npos;
}

struct LoadedEntries {
    std::deque<std::string> entries;  // <= CommandHistory::maxEntries
    bool droppedUnrecordable = false; // the file holds entries that must not be recorded
};

// The entries stored in `file` (missing file = none); entries that must not be recorded
// (CommandHistory::isRecordable, e.g. written by an older version) are dropped (review R3) and
// reported (review S4). Errors: io.read_failed.
Result<LoadedEntries> readEntries(const fs::path& file,
                                  std::span<const std::string_view> sensitivePrefixes) {
    LoadedEntries loaded;
    std::deque<std::string>& entries = loaded.entries;
    QFile input(toQString(toUtf8(file)));
    if (!input.exists()) {
        return loaded;
    }
    if (!input.open(QIODevice::ReadOnly)) {
        return std::unexpected(makeError(ErrorCode::IoReadFailed,
                                         toUtf8(file) + ": " + input.errorString().toStdString()));
    }
    while (!input.atEnd()) {
        // readLine() bounds the read; a line longer than an entry can be is skipped entirely.
        constexpr auto chunk = static_cast<qint64>(CommandHistory::maxEntryBytes) + 3;
        QByteArray line = input.readLine(chunk);
        if (!line.endsWith('\n') && !input.atEnd()) {
            while (!input.atEnd() && !input.readLine(chunk).endsWith('\n')) {
            }
            continue;
        }
        while (line.endsWith('\n') || line.endsWith('\r')) {
            line.chop(1);
        }
        std::string entry = line.toStdString();
        if (!isStorable(entry)) {
            continue;
        }
        if (!CommandHistory::isRecordable(entry, sensitivePrefixes)) {
            loaded.droppedUnrecordable = true;
            continue;
        }
        appendEntry(entries, std::move(entry));
    }
    return loaded;
}

} // namespace

fs::path CommandHistory::defaultFile(std::string_view profileId, std::string_view targetId) {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return fs::path(base.toStdU16String()) / "console" /
           std::format("history-{}-{}.txt", profileId, targetId);
}

CommandHistory::CommandHistory(fs::path file, std::span<const std::string_view> sensitivePrefixes)
    : m_file(std::move(file)), m_sensitivePrefixes(sensitivePrefixes) {}

Result<CommandHistory> CommandHistory::open(fs::path file,
                                            std::span<const std::string_view> sensitivePrefixes) {
    CommandHistory history(std::move(file), sensitivePrefixes);
    // Review R3: a file of an older version is tightened at once (also when nothing is recorded).
    if (QFile existing(toQString(toUtf8(history.m_file))); existing.exists()) {
        detail::restrictToOwner(existing);
    }
    auto loaded = readEntries(history.m_file, history.m_sensitivePrefixes);
    if (!loaded) {
        return std::unexpected(std::move(loaded).error());
    }
    history.m_entries = std::move(loaded->entries);
    // Review S4: secrets already in the file are removed from the disk at once (also when nothing
    // is recorded): the file is rewritten, under its lock, without them.
    if (loaded->droppedUnrecordable) {
        if (const Status rewritten = history.writeFile(); !rewritten) {
            qCWarning(log::lcDevice).noquote()
                << "Command history not cleaned:" << toQString(rewritten.error().detail);
        }
    }
    return history;
}

CommandHistory::~CommandHistory() {
    if (!m_pending.empty() && !m_file.empty()) {
        const Status status = save();
        if (!status) {
            qCWarning(log::lcDevice).noquote()
                << "Command history not saved:" << toQString(status.error().detail);
        }
    }
}

bool CommandHistory::isRecordable(std::string_view entry,
                                  std::span<const std::string_view> sensitivePrefixes) {
    return !entry.starts_with(' ') && !matchSensitiveCommand(entry, sensitivePrefixes);
}

Status CommandHistory::clearAll(const fs::path& directory) {
    std::error_code ec;
    if (!fs::is_directory(directory, ec)) {
        return {};
    }
    std::optional<Error> failure;
    std::size_t visited = 0;
    for (fs::directory_iterator it(directory, ec), end;
         !ec && it != end && visited < maxDirectoryEntries; it.increment(ec), ++visited) {
        const std::string name = toUtf8(it->path().filename());
        if (!name.starts_with("history-") || !name.ends_with(".txt")) {
            continue;
        }
        // Review R12: like save(), under the file's lock (another process may be merging).
        QLockFile lock(toQString(toUtf8(it->path())) + QStringLiteral(".lock"));
        if (!lock.tryLock(lockTimeout)) {
            if (!failure) {
                failure = makeError(ErrorCode::IoWriteFailed,
                                    toUtf8(it->path()) + ": locked by another program");
            }
            continue;
        }
        std::error_code removeError;
        if (!fs::remove(it->path(), removeError) && removeError && !failure) {
            failure = makeError(ErrorCode::IoWriteFailed,
                                toUtf8(it->path()) + ": " + removeError.message());
        }
    }
    if (failure) {
        return std::unexpected(std::move(*failure));
    }
    return {};
}

void CommandHistory::clear() noexcept {
    m_entries.clear();
    m_pending.clear();
    m_addsSinceSaveAttempt = 0;
}

void CommandHistory::add(std::string entry) {
    if (!isStorable(entry) || !isRecordable(entry, m_sensitivePrefixes)) {
        return;
    }
    appendEntry(m_entries, entry);
    m_pending.push_back(std::move(entry));
    while (m_pending.size() > maxEntries) {
        m_pending.pop_front();
    }
    // Every saveInterval-th add() attempts a save, whether earlier saves succeeded, were skipped
    // (lock timeout) or the pending list is capped (review fix 4).
    if (++m_addsSinceSaveAttempt >= saveInterval) {
        m_addsSinceSaveAttempt = 0;
        const Status status = save();
        if (!status) {
            qCWarning(log::lcDevice).noquote()
                << "Command history not saved:" << toQString(status.error().detail);
        }
    }
}

Status CommandHistory::save() {
    if (m_pending.empty()) {
        return {};
    }
    return writeFile();
}

Status CommandHistory::writeFile() {
    std::error_code ec;
    fs::create_directories(m_file.parent_path(), ec);
    QLockFile lock(toQString(toUtf8(m_file)) + QStringLiteral(".lock"));
    if (!lock.tryLock(m_lockTimeout)) {
        qCWarning(log::lcDevice).noquote()
            << "Command history save skipped: the file is locked by another program:"
            << toQString(toUtf8(m_file));
        return {};
    }
    std::deque<std::string> merged;
    if (auto loaded = readEntries(m_file, m_sensitivePrefixes)) {
        merged = std::move(loaded->entries);
    } else {
        // An unreadable file is replaced by our entries rather than blocking every save.
        qCWarning(log::lcDevice).noquote() << toQString(loaded.error().detail);
    }
    for (const std::string& entry : m_pending) {
        appendEntry(merged, entry);
    }

    // Review R9: QSaveFile creates its temporary file 0600 when the target exists (and then copies
    // the target's permissions), but with the umask for a new target. A new target is therefore
    // created empty and owner-only first, so no file of this history is ever group/world-readable.
    if (QFile target(toQString(toUtf8(m_file))); !target.exists()) {
        if (!target.open(QIODevice::WriteOnly | QIODevice::NewOnly,
                         QFileDevice::ReadOwner | QFileDevice::WriteOwner) &&
            !target.exists()) {
            return std::unexpected(
                makeError(ErrorCode::IoWriteFailed,
                          toUtf8(m_file) + ": " + target.errorString().toStdString()));
        }
    } else {
        detail::restrictToOwner(target);
    }
    QSaveFile output(toQString(toUtf8(m_file)));
    if (!output.open(QIODevice::WriteOnly)) {
        return std::unexpected(makeError(
            ErrorCode::IoWriteFailed, toUtf8(m_file) + ": " + output.errorString().toStdString()));
    }
    detail::restrictToOwner(output); // ADR-0186: the temporary file too, before any write
    for (const std::string& entry : merged) {
        output.write(entry.data(), static_cast<qint64>(entry.size()));
        output.write("\n", 1);
    }
    if (!output.commit()) {
        return std::unexpected(makeError(
            ErrorCode::IoWriteFailed, toUtf8(m_file) + ": " + output.errorString().toStdString()));
    }
    m_pending.clear(); // only after a successful write: failed saves are retried
    return {};
}

} // namespace hwio::device
