#pragma once

// HistoryStore - the console command histories of the GUI (plan/features/0005-m3-console.md, C4;
// ADR-0058).
//
// One SharedHistory per (profile, target), shared by every tab with that pair, so a command sent
// in one tab is found by Up / Ctrl+R in another. Each wraps the M1 CommandHistory (file
// <console directory>/history-<profile>-<target>.txt, the CLI's file; at most 1000 entries of at
// most 4 KiB; saved every 50 new entries and when the store is destroyed, locked and merged across
// processes). When the file cannot be read the history is kept in memory for this run (bounded by
// the same limit) and the failure is logged.
//
// Ownership: owned by AppViewModel (std::unique_ptr); a SharedHistory lives as long as the store
// or a console view model holds it (std::shared_ptr). GUI thread only.

#include "hio/device/command_history.h"

#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace hio::gui {

class SharedHistory {
public:
    explicit SharedHistory(std::filesystem::path file);
    ~SharedHistory(); // saves pending entries (errors are logged)
    SharedHistory(const SharedHistory&) = delete;
    SharedHistory& operator=(const SharedHistory&) = delete;
    SharedHistory(SharedHistory&&) = delete;
    SharedHistory& operator=(SharedHistory&&) = delete;

    // Adds a command (empty, multi-line or too long ones are ignored; consecutive duplicates
    // are collapsed).
    void add(const QString& command);
    // Oldest first, ≤ CommandHistory::maxEntries.
    [[nodiscard]] const std::deque<std::string>& entries() const;
    // Increases with every add (a navigation position is reset when it changed).
    [[nodiscard]] std::uint64_t revision() const noexcept { return m_revision; }
    // Newest first, unique, case-insensitive substring of `text` (all when empty), ≤ max.
    [[nodiscard]] QStringList search(const QString& text, std::size_t max) const;
    [[nodiscard]] bool persistent() const noexcept { return m_history.has_value(); }
    // Writes pending entries now (tests, quit). Errors are logged.
    void save();

private:
    std::optional<device::CommandHistory> m_history;
    std::deque<std::string> m_memory; // used when the file could not be read
    std::uint64_t m_revision = 0;
};

class HistoryStore {
public:
    // `directory`: where the history files are (the registry's console directory).
    explicit HistoryStore(std::filesystem::path directory);

    // The shared history of (profile, target); opened on first use.
    [[nodiscard]] std::shared_ptr<SharedHistory> history(const QString& profileId,
                                                         const QString& targetId);
    [[nodiscard]] std::filesystem::path fileFor(const QString& profileId,
                                                const QString& targetId) const;

private:
    std::filesystem::path m_directory;
    // Bounded by the number of (profile, target) pairs of the compiled-in profiles.
    std::map<std::pair<QString, QString>, std::shared_ptr<SharedHistory>> m_histories;
};

} // namespace hio::gui
