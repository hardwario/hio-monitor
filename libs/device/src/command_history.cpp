#include "hio/device/command_history.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QLockFile>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QtLogging>
#include <QtTypes>

#include <chrono>
#include <deque>
#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace hio::device {

namespace {

namespace fs = std::filesystem;

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

// The entries stored in `file` (missing file = none). Errors: io.read_failed.
Result<std::deque<std::string>> readEntries(const fs::path& file) {
    std::deque<std::string> entries;
    QFile input(toQString(toUtf8(file)));
    if (!input.exists()) {
        return entries;
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
        if (isStorable(entry)) {
            appendEntry(entries, std::move(entry));
        }
    }
    return entries;
}

} // namespace

fs::path CommandHistory::defaultFile(std::string_view profileId, std::string_view targetId) {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return fs::path(base.toStdU16String()) / "console" /
           std::format("history-{}-{}.txt", profileId, targetId);
}

CommandHistory::CommandHistory(fs::path file) : m_file(std::move(file)) {}

Result<CommandHistory> CommandHistory::open(fs::path file) {
    CommandHistory history(std::move(file));
    auto entries = readEntries(history.m_file);
    if (!entries) {
        return std::unexpected(std::move(entries).error());
    }
    history.m_entries = std::move(*entries);
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

void CommandHistory::add(std::string entry) {
    if (!isStorable(entry)) {
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
    std::error_code ec;
    fs::create_directories(m_file.parent_path(), ec);
    QLockFile lock(toQString(toUtf8(m_file)) + QStringLiteral(".lock"));
    if (!lock.tryLock(m_lockTimeout)) {
        qCWarning(log::lcDevice).noquote()
            << "Command history save skipped: the file is locked by another program:"
            << toQString(toUtf8(m_file));
        return {};
    }
    auto merged = readEntries(m_file);
    if (!merged) {
        // An unreadable file is replaced by our entries rather than blocking every save.
        qCWarning(log::lcDevice).noquote() << toQString(merged.error().detail);
        merged = std::deque<std::string>{};
    }
    for (const std::string& entry : m_pending) {
        appendEntry(*merged, entry);
    }

    QSaveFile output(toQString(toUtf8(m_file)));
    if (!output.open(QIODevice::WriteOnly)) {
        return std::unexpected(makeError(
            ErrorCode::IoWriteFailed, toUtf8(m_file) + ": " + output.errorString().toStdString()));
    }
    for (const std::string& entry : *merged) {
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

} // namespace hio::device
