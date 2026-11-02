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
// ADR-0186 (plan/features/0025): an entry that starts with a space or matches one of the sensitive
// command prefixes of the console's target (TargetProfile::sensitiveCommandPrefixes) is never
// stored (isRecordable); entries of older files that break this rule are dropped when the file is
// read (review R3). The file is owner-only (0600 on Unix: created so, tightened on open() and on
// every save; Windows: the per-user AppData ACL). Whether commands are recorded at all is the setting
// rememberCommandsSetting, checked by the callers (GUI and CLI console).
//
// Thread safety: none; one owner thread (the CLI / GUI thread).

#include "hwio/core/result.h"
#include "hwio/core/settings.h"

#include <chrono>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace hwio::device {

// ADR-0186: "Remember commands" (Settings > Console), shared by the GUI and the CLI console.
inline constexpr SettingKey<bool> rememberCommandsSetting{.path = "console/rememberCommands",
                                                          .defaultValue = true};

class CommandHistory {
public:
    static constexpr std::size_t maxEntries = 1000;
    static constexpr std::size_t maxEntryBytes = 4096;
    static constexpr std::size_t saveInterval = 50;
    static constexpr std::chrono::milliseconds lockTimeout{2000};

    [[nodiscard]] static std::filesystem::path defaultFile(std::string_view profileId,
                                                           std::string_view targetId);

    // Loads the file (a missing file is an empty history). `sensitivePrefixes` (the target's
    // TargetProfile::sensitiveCommandPrefixes, static storage) are never recorded. Errors:
    // io.read_failed.
    [[nodiscard]] static Result<CommandHistory>
    open(std::filesystem::path file, std::span<const std::string_view> sensitivePrefixes = {});

    // ADR-0186: false for an entry that must not be recorded: it starts with a space or matches a
    // sensitive prefix (matchSensitiveCommand).
    [[nodiscard]] static bool isRecordable(std::string_view entry,
                                           std::span<const std::string_view> sensitivePrefixes);

    // ADR-0186 ("Clear history"): deletes every history-*.txt file in `directory` (only those
    // names), each under its <file>.lock (waits at most lockTimeout, review R12). Errors:
    // io.write_failed (the first file that could not be deleted or was locked; the others are
    // still tried).
    [[nodiscard]] static Status clearAll(const std::filesystem::path& directory);

    CommandHistory(CommandHistory&& other) noexcept = default;
    CommandHistory& operator=(CommandHistory&& other) noexcept = default;
    CommandHistory(const CommandHistory&) = delete;
    CommandHistory& operator=(const CommandHistory&) = delete;
    // Saves pending entries (errors are logged).
    ~CommandHistory();

    // Adds an entry (empty, multi-line, too long or not recordable entries are ignored); saves at
    // every saveInterval-th new entry.
    void add(std::string entry);
    // Forgets the entries in memory, including those not saved yet (after clearAll()).
    void clear() noexcept;

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
    CommandHistory(std::filesystem::path file, std::span<const std::string_view> sensitivePrefixes);

    std::filesystem::path m_file;
    std::span<const std::string_view> m_sensitivePrefixes; // static storage (the profiles)
    std::deque<std::string> m_entries;                     // <= maxEntries
    std::deque<std::string> m_pending; // added since the last save, <= maxEntries
    std::chrono::milliseconds m_lockTimeout = lockTimeout;
    std::size_t m_addsSinceSaveAttempt = 0; // < saveInterval
};

} // namespace hwio::device
