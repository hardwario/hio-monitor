#pragma once

// CommandHistory - the console's command history (plan/features/0002-m1-jlink-rtt.md, ADR-0058).
//
// Bounds: at most maxEntries entries of at most maxEntryBytes bytes (longer commands are not
// stored); consecutive duplicates are collapsed. The file (UTF-8, one entry per line) is written
// atomically (QSaveFile) every saveInterval new entries and when the history is destroyed - never
// rewritten per command. Default file: AppLocalDataLocation/console/history-<profile>-<target>.txt.
//
// Several processes may use the same file (ADR-0058): save() takes QLockFile <file>.lock (waits at
// most lockTimeout), re-reads the file, appends this instance's new entries after the ones on
// disk, collapses consecutive duplicates, keeps the newest maxEntries and writes atomically. A lock
// timeout skips that save (log warning); the entries stay pending for the next save point. The
// in-memory list is not reloaded while running.
//
// Thread safety: none; one owner thread (the CLI / GUI thread).

#include "hio/core/result.h"

#include <chrono>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <string>
#include <string_view>

namespace hio::device {

class CommandHistory {
public:
    static constexpr std::size_t maxEntries = 1000;
    static constexpr std::size_t maxEntryBytes = 4096;
    static constexpr std::size_t saveInterval = 50;
    static constexpr std::chrono::milliseconds lockTimeout{2000};

    [[nodiscard]] static std::filesystem::path defaultFile(std::string_view profileId,
                                                           std::string_view targetId);

    // Loads the file (a missing file is an empty history). Errors: io.read_failed.
    [[nodiscard]] static Result<CommandHistory> open(std::filesystem::path file);

    CommandHistory(CommandHistory&& other) noexcept = default;
    CommandHistory& operator=(CommandHistory&& other) noexcept = default;
    CommandHistory(const CommandHistory&) = delete;
    CommandHistory& operator=(const CommandHistory&) = delete;
    // Saves pending entries (errors are logged).
    ~CommandHistory();

    // Adds an entry (empty, multi-line or too long entries are ignored); saves at every
    // saveInterval-th new entry.
    void add(std::string entry);

    // Merges the pending entries into the file under the file lock. A lock timeout is not an
    // error: the save is skipped and retried later. Errors: io.write_failed.
    [[nodiscard]] Status save();

    // Oldest first.
    [[nodiscard]] const std::deque<std::string>& entries() const noexcept { return m_entries; }
    [[nodiscard]] std::size_t pendingCount() const noexcept { return m_pending.size(); }
    [[nodiscard]] const std::filesystem::path& file() const noexcept { return m_file; }

    // Lock wait used by save() (tests shorten it).
    void setLockTimeout(std::chrono::milliseconds timeout) noexcept { m_lockTimeout = timeout; }

private:
    explicit CommandHistory(std::filesystem::path file);

    std::filesystem::path m_file;
    std::deque<std::string> m_entries; // <= maxEntries
    std::deque<std::string> m_pending; // added since the last save, <= maxEntries
    std::chrono::milliseconds m_lockTimeout = lockTimeout;
    std::size_t m_addsSinceSaveAttempt = 0; // < saveInterval
};

} // namespace hio::device
