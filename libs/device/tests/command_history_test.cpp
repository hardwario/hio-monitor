#include "hwio/core/error.h"
#include "hwio/device/command_history.h"
#include "hwio/device/profile.h"

#include <QFile>
#include <QIODevice>
#include <QLockFile>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hwio::device::CommandHistory;

class History : public ::testing::Test {
protected:
    [[nodiscard]] fs::path file() const {
        return fs::path(m_dir.path().toStdString()) / "console" / "history-chester-app.txt";
    }

    [[nodiscard]] std::vector<std::string> fileLines() const {
        std::vector<std::string> lines;
        std::ifstream input(file());
        for (std::string line; std::getline(input, line);) {
            lines.push_back(line);
        }
        return lines;
    }

private:
    QTemporaryDir m_dir;
};

TEST_F(History, DefaultFileName) {
    const fs::path path = CommandHistory::defaultFile("chester", "app");
    EXPECT_EQ(path.filename(), "history-chester-app.txt");
    EXPECT_EQ(path.parent_path().filename(), "console");
}

TEST_F(History, MissingFileIsEmpty) {
    auto history = CommandHistory::open(file());
    ASSERT_TRUE(history);
    EXPECT_TRUE(history->entries().empty());
}

TEST_F(History, CollapsesConsecutiveDuplicatesAndIgnoresInvalidEntries) {
    auto history = CommandHistory::open(file());
    ASSERT_TRUE(history);
    history->add("version");
    history->add("version");
    history->add("");
    history->add("two\nlines");
    history->add(std::string(CommandHistory::maxEntryBytes + 1, 'x'));
    history->add("status");
    history->add("version");
    EXPECT_EQ(history->entries(), (std::deque<std::string>{"version", "status", "version"}));
}

// ADR-0186 (review M-4b): leading-space and sensitive commands are never recorded.
TEST_F(History, LeadingSpaceAndSensitiveCommandsAreNotRecorded) {
    const auto prefixes = hwio::device::sensitivePrefixesOf("chester", "app");
    ASSERT_EQ(prefixes.size(), 6U);
    {
        auto history = CommandHistory::open(file(), prefixes);
        ASSERT_TRUE(history);
        history->add(" lte config password hunter2"); // leading space: anything
        history->add(" version");
        history->add("lrw config appkey 00112233");
        history->add("lrw\tconfig  nwkskey 00"); // whitespace-normalised words
        history->add("lrw config appskey");      // without a value too
        history->add("lte config username me");
        history->add("lte config password pw");
        history->add("ble config passkey 123456");
        history->add("LTE config password pw2"); // case-sensitive: recorded
        history->add("lrw config appkeys x");    // another word: recorded
        history->add("lrw config show");
        history->add("info claim-token"); // only prints a secret: recorded
        EXPECT_EQ(history->entries(),
                  (std::deque<std::string>{"LTE config password pw2", "lrw config appkeys x",
                                           "lrw config show", "info claim-token"}));
    }
    const std::vector<std::string> lines = fileLines();
    EXPECT_EQ(lines, (std::vector<std::string>{"LTE config password pw2", "lrw config appkeys x",
                                               "lrw config show", "info claim-token"}));
}

TEST(SensitiveCommands, MatchesWholeLeadingWords) {
    constexpr std::array<std::string_view, 2> prefixes{"lte config password", "ble config passkey"};
    using hwio::device::matchSensitiveCommand;
    EXPECT_EQ(matchSensitiveCommand("lte config password x", prefixes), "lte config password");
    EXPECT_EQ(matchSensitiveCommand("  ble   config\tpasskey 1", prefixes), "ble config passkey");
    EXPECT_FALSE(matchSensitiveCommand("lte config", prefixes));
    EXPECT_FALSE(matchSensitiveCommand("lte config passwords", prefixes));
    EXPECT_FALSE(matchSensitiveCommand("xlte config password", prefixes));
    EXPECT_FALSE(matchSensitiveCommand("", prefixes));
    EXPECT_TRUE(hwio::device::sensitivePrefixesOf("chester", "lte").empty());
    EXPECT_TRUE(hwio::device::sensitivePrefixesOf("nope", "app").empty());
}

#ifndef _WIN32
// ADR-0186 (review M-4a): the history file is owner-only, also when it existed before.
TEST_F(History, FileIsOwnerOnly) {
    fs::create_directories(file().parent_path());
    std::ofstream(file()) << "old\n";
    fs::permissions(file(), fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                                fs::perms::others_read);
    {
        auto history = CommandHistory::open(file());
        ASSERT_TRUE(history);
        history->add("version");
    }
    EXPECT_EQ(fs::status(file()).permissions() & fs::perms::all,
              fs::perms::owner_read | fs::perms::owner_write);
}
#endif

#ifndef _WIN32
// Review R3: a file of an older version (0644, with a sensitive entry) is tightened when it is
// opened and its sensitive entries are neither offered nor written back.
TEST_F(History, OldFileIsFilteredAndTightenedOnOpen) {
    fs::create_directories(file().parent_path());
    std::ofstream(file()) << "version\nlte config password hunter2\n leading space\nstatus\n";
    fs::permissions(file(), fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                                fs::perms::others_read);
    {
        auto history =
            CommandHistory::open(file(), hwio::device::sensitivePrefixesOf("chester", "app"));
        ASSERT_TRUE(history);
        EXPECT_EQ(fs::status(file()).permissions() & fs::perms::all,
                  fs::perms::owner_read | fs::perms::owner_write);
        EXPECT_EQ(history->entries(), (std::deque<std::string>{"version", "status"}));
        history->add("uptime");
    }
    EXPECT_EQ(fileLines(), (std::vector<std::string>{"version", "status", "uptime"}));
}
#endif

#ifndef _WIN32
// Review R9: a new history file is created 0600, and QSaveFile keeps it 0600 when it replaces the
// existing file on later saves (the pre-create approach relies on that QSaveFile behaviour).
TEST_F(History, SavesKeepTheFileOwnerOnly) {
    const fs::perms ownerOnly = fs::perms::owner_read | fs::perms::owner_write;
    auto history = CommandHistory::open(file());
    ASSERT_TRUE(history);
    history->add("first");
    ASSERT_TRUE(history->save()); // creates the file
    EXPECT_EQ(fs::status(file()).permissions() & fs::perms::all, ownerOnly);
    for (const char* entry : {"second", "third"}) {
        history->add(entry);
        ASSERT_TRUE(history->save()); // replaces the existing file
        EXPECT_EQ(fs::status(file()).permissions() & fs::perms::all, ownerOnly) << entry;
    }
    EXPECT_EQ(fileLines(), (std::vector<std::string>{"first", "second", "third"}));
}
#endif

// Review R12: a history file whose lock another process holds is not deleted.
TEST_F(History, ClearAllRespectsTheFileLock) {
    const fs::path directory = file().parent_path();
    fs::create_directories(directory);
    std::ofstream(file()) << "x\n";
    QLockFile lock(QString::fromStdString(file().string() + ".lock"));
    ASSERT_TRUE(lock.tryLock(0));
    const auto cleared = CommandHistory::clearAll(directory);
    ASSERT_FALSE(cleared);
    EXPECT_EQ(cleared.error().code, hwio::ErrorCode::IoWriteFailed);
    EXPECT_TRUE(fs::exists(file()));
    lock.unlock();
    EXPECT_TRUE(CommandHistory::clearAll(directory));
    EXPECT_FALSE(fs::exists(file()));
}

// ADR-0186 ("Clear history"): only history-*.txt files are deleted; clear() forgets the entries.
TEST_F(History, ClearAllDeletesOnlyHistoryFiles) {
    const fs::path directory = file().parent_path();
    fs::create_directories(directory);
    for (const char* name : {"history-chester-app.txt", "history-other-x.txt"}) {
        std::ofstream(directory / name) << "x\n";
    }
    for (const char* name :
         {"chester-app-20260101-000000-1.log", "history-notes.md", "my-history.txt"}) {
        std::ofstream(directory / name) << "keep\n";
    }
    auto history = CommandHistory::open(file());
    ASSERT_TRUE(history);
    history->add("pending");
    ASSERT_TRUE(CommandHistory::clearAll(directory));
    history->clear();
    EXPECT_TRUE(history->entries().empty());
    EXPECT_EQ(history->pendingCount(), 0U);
    EXPECT_FALSE(fs::exists(directory / "history-chester-app.txt"));
    EXPECT_FALSE(fs::exists(directory / "history-other-x.txt"));
    for (const char* name :
         {"chester-app-20260101-000000-1.log", "history-notes.md", "my-history.txt"}) {
        EXPECT_TRUE(fs::exists(directory / name)) << name;
    }
    EXPECT_TRUE(CommandHistory::clearAll(directory / "missing")); // nothing to delete
}

TEST_F(History, SavedOnDestructionAndLoadedAgain) {
    {
        auto history = CommandHistory::open(file());
        ASSERT_TRUE(history);
        history->add("version");
        history->add("lte state");
        EXPECT_FALSE(fs::exists(file())); // not rewritten per command
    }
    EXPECT_EQ(fileLines(), (std::vector<std::string>{"version", "lte state"}));
    auto again = CommandHistory::open(file());
    ASSERT_TRUE(again);
    EXPECT_EQ(again->entries(), (std::deque<std::string>{"version", "lte state"}));
}

TEST_F(History, SavedEvery50Entries) {
    auto history = CommandHistory::open(file());
    ASSERT_TRUE(history);
    for (std::size_t i = 0; i < CommandHistory::saveInterval - 1; ++i) {
        history->add("cmd " + std::to_string(i));
    }
    EXPECT_FALSE(fs::exists(file()));
    history->add("cmd 49");
    EXPECT_EQ(fileLines().size(), CommandHistory::saveInterval);
    EXPECT_EQ(history->pendingCount(), 0U);
}

TEST_F(History, BoundedTo1000NewestEntries) {
    auto history = CommandHistory::open(file());
    ASSERT_TRUE(history);
    for (std::size_t i = 0; i < CommandHistory::maxEntries + 20; ++i) {
        history->add("cmd " + std::to_string(i));
    }
    EXPECT_EQ(history->entries().size(), CommandHistory::maxEntries);
    EXPECT_EQ(history->entries().front(), "cmd 20");
    ASSERT_TRUE(history->save());
    const auto lines = fileLines();
    EXPECT_EQ(lines.size(), CommandHistory::maxEntries);
    EXPECT_EQ(lines.back(), "cmd 1019");
}

TEST_F(History, TwoWritersAreMergedWithoutLosingEntries) {
    auto first = CommandHistory::open(file());
    auto second = CommandHistory::open(file());
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    first->add("a1");
    second->add("b1");
    first->add("a2");
    second->add("b2");
    ASSERT_TRUE(first->save());
    ASSERT_TRUE(second->save());
    EXPECT_EQ(fileLines(), (std::vector<std::string>{"a1", "a2", "b1", "b2"}));
    // The in-memory lists are not reloaded while running.
    EXPECT_EQ(first->entries(), (std::deque<std::string>{"a1", "a2"}));
    first->add("b2"); // a duplicate of the last entry on disk is collapsed by the merge
    ASSERT_TRUE(first->save());
    EXPECT_EQ(fileLines(), (std::vector<std::string>{"a1", "a2", "b1", "b2"}));
}

TEST_F(History, LockTimeoutSkipsTheSaveAndRetriesLater) {
    auto history = CommandHistory::open(file());
    ASSERT_TRUE(history);
    history->setLockTimeout(std::chrono::milliseconds(50));
    history->add("kept");
    fs::create_directories(file().parent_path());
    {
        QLockFile other(QString::fromStdString(file().string() + ".lock"));
        ASSERT_TRUE(other.tryLock(0));
        // A lock of this process: the save waits 50 ms and gives up without an error.
        EXPECT_TRUE(history->save());
        EXPECT_FALSE(fs::exists(file()));
        EXPECT_EQ(history->pendingCount(), 1U);
    }
    ASSERT_TRUE(history->save());
    EXPECT_EQ(fileLines(), (std::vector<std::string>{"kept"}));
}

// Review fix 4 (2026-10-09): with a held lock and more than 1000 pending entries, a save (and its
// lock wait) is attempted only every 50 adds, not on every add.
TEST_F(History, SaveAttemptsStayAtEvery50AddsWhenSavesAreSkipped) {
    auto history = CommandHistory::open(file());
    ASSERT_TRUE(history);
    history->setLockTimeout(std::chrono::milliseconds(20));
    fs::create_directories(file().parent_path());
    QLockFile other(QString::fromStdString(file().string() + ".lock"));
    ASSERT_TRUE(other.tryLock(0));
    for (std::size_t i = 0; i < CommandHistory::maxEntries; ++i) {
        history->add("cmd " + std::to_string(i));
    }
    EXPECT_EQ(history->pendingCount(), CommandHistory::maxEntries); // capped, saves skipped
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < CommandHistory::saveInterval - 1; ++i) {
        history->add("more " + std::to_string(i));
    }
    // 49 adds at the cap: no save attempt, so no 20 ms lock waits (49 attempts would take
    // 980 ms; the bound is half of that, review R46).
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(490));
    other.unlock();
    history->add("the 50th");
    EXPECT_EQ(history->pendingCount(), 0U);
    EXPECT_EQ(fileLines().size(), CommandHistory::maxEntries);
}

TEST_F(History, OverlongAndEmptyLinesOnDiskAreSkipped) {
    fs::create_directories(file().parent_path());
    {
        std::ofstream output(file());
        output << "ok\n\n" << std::string(CommandHistory::maxEntryBytes + 5, 'y') << "\nok2\r\n";
    }
    auto history = CommandHistory::open(file());
    ASSERT_TRUE(history);
    EXPECT_EQ(history->entries().front(), "ok");
    EXPECT_EQ(history->entries().back(), "ok2");
}

} // namespace
