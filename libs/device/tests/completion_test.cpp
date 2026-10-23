// Plan 0021 (ADR-0166, ADR-0170): parseInfoShow, CommandTree + parseHelp, CommandTreeCache (bounds,
// LRU, two-process lock) and Completer.

#include "hio/core/error.h"
#include "hio/device/command_tree.h"
#include "hio/device/command_tree_cache.h"
#include "hio/device/completer.h"
#include "hio/device/device_info.h"
#include "lock_holder.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hio::device::CommandEntry;
using hio::device::CommandTree;
using hio::device::CommandTreeCache;
using hio::device::Completer;
using hio::device::FirmwareKey;
using hio::device::parseHelp;
using hio::device::parseInfoShow;
using hio::device::Suggestion;

// ---- info show -----------------------------------------------------------------------------

TEST(InfoShow, ParsesFields) {
    const std::vector<std::string> lines{
        "uart:~$ info show",
        "vendor-name: HARDWARIO",
        "product-name: CHESTER-M",
        "hw-variant: CDGLS",
        "fw-name: hio-chester-push-fm",
        "fw-version: v4.0.1",
        "serial-number: \"2159018247\"",
        "\x1b[1;32mclaim-token:\x1b[0m abc",
        "",
        "Some text without a field",
        "fw-name: duplicate",
    };
    const auto info = parseInfoShow(lines);
    EXPECT_EQ(info.fwName(), "hio-chester-push-fm");
    EXPECT_EQ(info.fwVersion(), "v4.0.1");
    EXPECT_TRUE(info.hasFirmware());
    EXPECT_EQ(info.value("serial-number"), "2159018247"); // quotes removed
    EXPECT_EQ(info.value("claim-token"), "abc");          // ANSI removed
    EXPECT_FALSE(info.value("uart"));                     // the prompt is not a field
    EXPECT_EQ(info.fields.size(), 7U);
    EXPECT_FALSE(parseInfoShow(std::vector<std::string>{"fw-name: x"}).hasFirmware());
}

TEST(InfoShow, Bounded) {
    std::vector<std::string> lines;
    lines.reserve(102);
    for (int i = 0; i < 100; ++i) {
        lines.push_back(std::format("key-{}: {}", i, std::string(1000, 'v')));
    }
    lines.push_back(std::string(100, 'k') + ": too long key");
    // A multi-byte character across the cut is not split.
    lines.insert(lines.begin(), "utf: " + std::string(255, 'a') + "\xc3\xa9");
    const auto info = parseInfoShow(lines);
    ASSERT_EQ(info.fields.size(), hio::device::DeviceInfo::maxFields);
    EXPECT_EQ(info.fields.front().value.size(), 255U);
    for (const auto& field : info.fields) {
        EXPECT_LE(field.value.size(), hio::device::DeviceInfo::maxFieldBytes);
    }
}

// ---- parseHelp -----------------------------------------------------------------------------

TEST(ParseHelp, TopLevel) {
    const std::vector<std::string> lines{
        "Please press the <Tab> button to see all available commands.",
        "You can also use the <Tab> button to prompt or auto-complete all commands.",
        "",
        "Shell supports following meta-keys:",
        "  Ctrl + (a key from: abcdefklnpuw)",
        "",
        "Available commands:",
        "  app       :Application commands.",
        "  clear     :Clear screen.",
        "  device    : Device commands",
        "  kernel    :Kernel commands",
        "  lte       :LTE commands that wrap",
        "             onto a second line.",
        "  bad name  :ignored",
        "uart:~$ ",
    };
    const auto entries = parseHelp(lines);
    ASSERT_EQ(entries.size(), 5U);
    EXPECT_EQ(entries[0].name, "app");
    EXPECT_EQ(entries[0].help, "Application commands.");
    EXPECT_EQ(entries[2].name, "device");
    EXPECT_EQ(entries[2].help, "Device commands");
    EXPECT_EQ(entries[4].name, "lte");
    EXPECT_EQ(entries[4].help, "LTE commands that wrap onto a second line.");
}

TEST(ParseHelp, SubcommandsAndAnsi) {
    const std::vector<std::string> lines{
        "lte - LTE commands",
        "Subcommands:",
        "\x1b[1;32m  config\x1b[0m  :Configuration commands.\r",
        "  test    :Test commands.",
        "  test    :duplicate is kept by parseHelp, dropped by the tree",
    };
    const auto entries = parseHelp(lines);
    ASSERT_EQ(entries.size(), 3U);
    EXPECT_EQ(entries[0].name, "config");
    EXPECT_EQ(entries[0].help, "Configuration commands.");
    EXPECT_TRUE(parseHelp(std::vector<std::string>{"uart:~$ help", "no list here"}).empty());
}

// ---- CommandTree ---------------------------------------------------------------------------

CommandTree sampleTree() {
    CommandTree tree;
    const std::vector<CommandEntry> top{{.name = "app", .help = "Application commands."},
                                        {.name = "lte", .help = "LTE commands"},
                                        {.name = "led", .help = "LED commands"},
                                        {.name = "lte", .help = "duplicate"},
                                        {.name = "bad name", .help = "invalid"}};
    EXPECT_TRUE(tree.setChildren({}, top));
    const std::vector<std::string> lte{"lte"};
    const std::vector<CommandEntry> lteChildren{{.name = "config", .help = "Configuration"},
                                                {.name = "connect", .help = "Connect"},
                                                {.name = "test", .help = "Test"}};
    EXPECT_TRUE(tree.setChildren(lte, lteChildren));
    return tree;
}

TEST(CommandTreeTest, ChildrenAndJsonRoundTrip) {
    const CommandTree tree = sampleTree();
    EXPECT_EQ(tree.root().children.size(), 3U);
    EXPECT_EQ(tree.nodeCount(), 6U);
    const std::vector<std::string> lte{"lte"};
    ASSERT_NE(tree.find(lte), nullptr);
    EXPECT_TRUE(tree.find(lte)->childrenKnown);
    const std::vector<std::string> missing{"nope"};
    EXPECT_EQ(tree.find(missing), nullptr);
    CommandTree copy = tree;
    EXPECT_FALSE(copy.setChildren(missing, {}));

    const auto restored = CommandTree::fromJson(tree.toJson());
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->nodeCount(), 6U);
    EXPECT_EQ(restored->find(lte)->children.size(), 3U);
    const std::vector<std::string> app{"app"};
    EXPECT_FALSE(restored->find(app)->childrenKnown);
    EXPECT_FALSE(CommandTree::fromJson(QJsonObject{}));
}

TEST(CommandTreeTest, NodeBound) {
    CommandTree tree;
    std::vector<CommandEntry> many;
    many.reserve(CommandTree::maxNodes + 10);
    for (std::size_t i = 0; i < CommandTree::maxNodes + 10; ++i) {
        many.push_back(CommandEntry{.name = std::format("c{}", i), .help = std::string(1000, 'h')});
    }
    EXPECT_TRUE(tree.setChildren({}, many));
    EXPECT_EQ(tree.nodeCount(), CommandTree::maxNodes);
    EXPECT_EQ(tree.root().children.front().help.size(), CommandTree::maxHelpBytes);
    // Replacing children frees their nodes.
    const std::vector<CommandEntry> few{{.name = "a", .help = {}}};
    EXPECT_TRUE(tree.setChildren({}, few));
    EXPECT_EQ(tree.nodeCount(), 1U);
}

// ---- CommandTreeCache ----------------------------------------------------------------------

class TreeCache : public ::testing::Test {
protected:
    [[nodiscard]] fs::path dir() const { return fs::path(m_dir.path().toStdU16String()) / "trees"; }
    static FirmwareKey key(const std::string& version) {
        return FirmwareKey{.profileId = "chester",
                           .targetId = "app",
                           .fwName = "hio-chester-push-fm",
                           .fwVersion = version};
    }

private:
    QTemporaryDir m_dir;
};

TEST_F(TreeCache, StoreAndLoad) {
    CommandTreeCache cache(dir());
    EXPECT_FALSE(cache.load(key("v1")));
    ASSERT_TRUE(cache.store(key("v1"), sampleTree()));
    const auto loaded = cache.load(key("v1"));
    EXPECT_EQ(loaded.transform([](const CommandTree& tree) { return tree.nodeCount(); }),
              std::optional<std::size_t>(6U));
    EXPECT_FALSE(cache.load(key("v2"))); // another firmware
    EXPECT_NE(cache.fileFor(key("v1")), cache.fileFor(key("v2")));
    EXPECT_TRUE(cache.fileFor(key("v1")).filename().string().starts_with("chester-app-"));
}

TEST_F(TreeCache, DamagedOrForeignFilesAreMisses) {
    CommandTreeCache cache(dir());
    ASSERT_TRUE(cache.store(key("v1"), sampleTree()));
    {
        std::ofstream out(cache.fileFor(key("v1")), std::ios::trunc);
        out << "{ not json";
    }
    EXPECT_FALSE(cache.load(key("v1")));
    // A file of another key at this name (hash collision, edited file) is not used.
    ASSERT_TRUE(cache.store(key("v2"), sampleTree()));
    std::error_code ec;
    fs::copy_file(cache.fileFor(key("v2")), cache.fileFor(key("v1")),
                  fs::copy_options::overwrite_existing, ec);
    ASSERT_FALSE(ec);
    EXPECT_FALSE(cache.load(key("v1")));
}

TEST_F(TreeCache, TooLargeIsRefused) {
    CommandTreeCache cache(dir());
    CommandTree big;
    std::vector<CommandEntry> many;
    many.reserve(1000);
    for (std::size_t i = 0; i < 1000; ++i) {
        many.push_back(
            CommandEntry{.name = std::format("command{}", i), .help = std::string(200, 'h')});
    }
    ASSERT_TRUE(big.setChildren({}, many));
    const auto stored = cache.store(key("v1"), big);
    ASSERT_FALSE(stored);
    EXPECT_EQ(stored.error().code, hio::ErrorCode::UsageInvalidArgument);
    EXPECT_FALSE(fs::exists(cache.fileFor(key("v1"))));
}

TEST_F(TreeCache, LeastRecentlyUsedBeyondTwentyAreEvicted) {
    CommandTreeCache cache(dir());
    const auto old = fs::file_time_type::clock::now() - std::chrono::hours(1);
    for (std::size_t i = 0; i < CommandTreeCache::maxFirmwares; ++i) {
        ASSERT_TRUE(cache.store(key(std::format("v{}", i)), sampleTree()));
        std::error_code ec;
        fs::last_write_time(cache.fileFor(key(std::format("v{}", i))),
                            old + std::chrono::seconds(i), ec);
    }
    // v0 is the oldest; loading it makes v1 the least recently used.
    ASSERT_TRUE(cache.load(key("v0")));
    ASSERT_TRUE(cache.store(key("new"), sampleTree()));
    EXPECT_TRUE(fs::exists(cache.fileFor(key("v0"))));
    EXPECT_FALSE(fs::exists(cache.fileFor(key("v1"))));
    EXPECT_TRUE(fs::exists(cache.fileFor(key("new"))));
    std::size_t files = 0;
    for (const auto& entry : fs::directory_iterator(dir())) {
        files += entry.path().extension() == ".json" ? 1 : 0;
    }
    EXPECT_EQ(files, CommandTreeCache::maxFirmwares);
}

TEST_F(TreeCache, LockHeldByAnotherProcessSkipsTheStore) {
    CommandTreeCache cache(dir());
    cache.setLockTimeout(std::chrono::milliseconds(100));
    fs::create_directories(dir());
    hio::device::test::LockHolder holder(QString::fromStdU16String((dir() / ".lock").u16string()));
    ASSERT_TRUE(holder.waitUntilLocked());
    const auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(cache.store(key("v1"), sampleTree())); // skipped, not an error
    EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(90));
    EXPECT_FALSE(fs::exists(cache.fileFor(key("v1"))));
    holder.release();
    EXPECT_TRUE(cache.store(key("v1"), sampleTree()));
    EXPECT_TRUE(fs::exists(cache.fileFor(key("v1"))));
}

// ---- Completer -----------------------------------------------------------------------------

std::vector<std::string> texts(const std::vector<Suggestion>& rows) {
    std::vector<std::string> result;
    result.reserve(rows.size());
    for (const Suggestion& row : rows) {
        result.push_back(row.text);
    }
    return result;
}

TEST(CompleterTest, CommandsFirstThenHistoryNewestFirst) {
    const CommandTree tree = sampleTree();
    const Completer completer(&tree);
    const std::deque<std::string> history{"lte test", "led on", "lte config show", "lte test",
                                          "app"};
    const auto rows = completer.suggest("l", history);
    EXPECT_EQ(texts(rows),
              (std::vector<std::string>{"lte ", "led ", "lte test", "lte config show", "led on"}));
    EXPECT_EQ(rows[0].kind, Suggestion::Kind::Command);
    EXPECT_EQ(rows[0].label, "lte");
    EXPECT_EQ(rows[0].help, "LTE commands");
    EXPECT_EQ(rows[2].kind, Suggestion::Kind::History);
    EXPECT_TRUE(rows[2].help.empty());
    // Subcommands after a known command.
    EXPECT_EQ(texts(completer.suggest("lte c", {})),
              (std::vector<std::string>{"lte config ", "lte connect "}));
    EXPECT_EQ(texts(completer.suggest("lte ", {})),
              (std::vector<std::string>{"lte config ", "lte connect ", "lte test "}));
    // Unknown children, unknown words, an empty input: nothing from the tree.
    EXPECT_TRUE(completer.suggest("app x", {}).empty());
    EXPECT_TRUE(completer.suggest("nope ", {}).empty());
    EXPECT_TRUE(completer.suggest("", history).empty());
    EXPECT_TRUE(completer.suggest("   ", history).empty());
    // The input itself is not suggested from history.
    EXPECT_TRUE(Completer().suggest("app", history).empty());
}

TEST(CompleterTest, CapAndHistoryOnly) {
    std::deque<std::string> history;
    for (int i = 0; i < 200; ++i) {
        history.push_back(std::format("cmd {}", i));
    }
    const Completer completer;
    const auto rows = completer.suggest("cmd", history);
    ASSERT_EQ(rows.size(), Completer::maxSuggestions);
    EXPECT_EQ(rows.front().text, "cmd 199");
    EXPECT_EQ(completer.suggest("cmd", history, 3).size(), 3U);
}

TEST(CompleterTest, TabCompletion) {
    const CommandTree tree = sampleTree();
    const Completer completer(&tree);
    EXPECT_EQ(completer.complete("a").replacement, "app ");
    const auto common = completer.complete("l");
    EXPECT_EQ(common.replacement, "l"); // "lte", "led": no longer common prefix
    EXPECT_EQ(common.candidates, (std::vector<std::string>{"lte", "led"}));
    EXPECT_EQ(completer.complete("lte co").replacement, "lte con");
    EXPECT_EQ(completer.complete("lte con").candidates,
              (std::vector<std::string>{"config", "connect"}));
    EXPECT_EQ(completer.complete("lte t").replacement, "lte test ");
    EXPECT_EQ(completer.complete("zzz").replacement, "zzz");
    EXPECT_TRUE(completer.complete("zzz").candidates.empty());
    EXPECT_EQ(Completer().complete("a").replacement, "a");
}

TEST(CompleterTest, HistoryHintAndPathToLearn) {
    const std::deque<std::string> history{"lte test", "lte config show", "led"};
    EXPECT_EQ(Completer::historyHint("lte", history), "lte config show");
    EXPECT_EQ(Completer::historyHint("lte t", history), "lte test");
    EXPECT_FALSE(Completer::historyHint("led", history)); // not longer than the input
    EXPECT_FALSE(Completer::historyHint("", history));

    const CommandTree tree = sampleTree();
    const Completer completer(&tree);
    EXPECT_EQ(completer.pathToLearn("app "), (std::vector<std::string>{"app"}));
    EXPECT_EQ(completer.pathToLearn("app x"), (std::vector<std::string>{"app"}));
    EXPECT_FALSE(completer.pathToLearn("lte c"));  // known children
    EXPECT_FALSE(completer.pathToLearn("ap"));     // still typing the first word
    EXPECT_FALSE(completer.pathToLearn("nope x")); // unknown command
    EXPECT_FALSE(Completer().pathToLearn("app "));
}

} // namespace
