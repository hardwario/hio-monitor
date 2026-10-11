#include "hio/device/command_history.h"

#include <QFile>
#include <QIODevice>
#include <QLockFile>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hio::device::CommandHistory;

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
