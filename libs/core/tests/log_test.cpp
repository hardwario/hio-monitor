#include "hio/core/error.h"
#include "hio/core/log.h"

#include <QLoggingCategory>
#include <QString>
#include <QTemporaryDir>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hio::log::AppKind;
using hio::log::Level;
using hio::log::Logging;

fs::path tempPath(const QTemporaryDir& dir) { return {dir.path().toStdU16String()}; }

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> result;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);) {
        result.push_back(line);
    }
    return result;
}

std::vector<fs::path> ownLogFiles(const fs::path& dir) {
    std::vector<fs::path> files;
    const std::regex pattern(R"(^(gui|cli)-\d{8}-\d{6}-\d+(\.part\d+)?\.log$)");
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (std::regex_match(entry.path().filename().string(), pattern)) {
            files.push_back(entry.path());
        }
    }
    return files;
}

std::unique_ptr<Logging> initOrFail(AppKind kind, const fs::path& dir,
                                    Level stderrLevel = Level::Critical) {
    auto logging = Logging::init(kind, {.stderrLevel = stderrLevel, .logDir = dir});
    EXPECT_TRUE(logging.has_value());
    return logging ? std::move(*logging) : nullptr;
}

TEST(Logging, WritesFormattedLinesToFile) {
    const QTemporaryDir dir;
    auto logging = initOrFail(AppKind::Cli, tempPath(dir));
    ASSERT_TRUE(logging);
    const fs::path file = logging->currentFile();
    EXPECT_TRUE(
        std::regex_match(file.filename().string(), std::regex(R"(^cli-\d{8}-\d{6}-\d+\.log$)")))
        << file;
    qCDebug(hio::log::lcCore) << "debug line";
    qCWarning(hio::log::lcCore) << "warning line";
    logging.reset();

    const std::string content = readFile(file);
    const std::regex linePattern(
        R"(^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z \[(dbg|inf|wrn|err|crt)\] [\w.]+ \(0x[0-9a-f]+\) .*$)");
    for (const std::string& line : lines(content)) {
        EXPECT_TRUE(std::regex_match(line, linePattern)) << line;
    }
    EXPECT_NE(content.find("[dbg] hio.core"), std::string::npos);
    EXPECT_NE(content.find("debug line"), std::string::npos);
    EXPECT_NE(content.find("[wrn] hio.core"), std::string::npos);
}

TEST(Logging, GuiFilePrefix) {
    const QTemporaryDir dir;
    auto logging = initOrFail(AppKind::Gui, tempPath(dir));
    ASSERT_TRUE(logging);
    EXPECT_TRUE(logging->currentFile().filename().string().starts_with("gui-"));
}

TEST(Logging, SecondInstanceIsRejected) {
    const QTemporaryDir dir;
    auto first = initOrFail(AppKind::Cli, tempPath(dir));
    ASSERT_TRUE(first);
    const auto second = Logging::init(AppKind::Cli, {.logDir = tempPath(dir)});
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, hio::ErrorCode::InternalUnexpected);
}

TEST(Logging, StderrThreshold) {
    const QTemporaryDir dir;
    auto logging = initOrFail(AppKind::Cli, tempPath(dir), Level::Warning);
    ASSERT_TRUE(logging);
    ::testing::internal::CaptureStderr();
    qCInfo(hio::log::lcCore) << "quiet info";
    qCWarning(hio::log::lcCore) << "loud warning";
    const std::string captured = ::testing::internal::GetCapturedStderr();
    EXPECT_EQ(captured.find("quiet info"), std::string::npos);
    EXPECT_NE(captured.find("loud warning"), std::string::npos);
    logging.reset();
}

TEST(Logging, TruncatesLongMessages) {
    const QTemporaryDir dir;
    auto logging = initOrFail(AppKind::Cli, tempPath(dir));
    ASSERT_TRUE(logging);
    const fs::path file = logging->currentFile();
    // Multi-byte characters around the cut point must not be split.
    qCInfo(hio::log::lcCore).noquote() << QString(qsizetype{100} * 1024, QChar(0x017E)); // 'ž'
    logging.reset();

    const std::vector<std::string> all = lines(readFile(file));
    ASSERT_FALSE(all.empty());
    const std::string& last = all.back();
    EXPECT_TRUE(last.ends_with(" [truncated]"));
    const std::size_t messageStart = last.find(") ") + 2;
    EXPECT_LE(last.size() - messageStart, hio::log::maxMessageBytes);
    const std::string message = last.substr(messageStart, last.size() - messageStart - 12);
    EXPECT_EQ(message.size() % 2, 0U); // whole 2-byte sequences only
}

TEST(Logging, RotatesAtFileSizeLimit) {
    const QTemporaryDir dir;
    auto logging = initOrFail(AppKind::Cli, tempPath(dir));
    ASSERT_TRUE(logging);
    const fs::path first = logging->currentFile();
    const QString chunk(qsizetype{60} * 1024, QLatin1Char('x'));
    for (int i = 0; i < 200; ++i) { // ~12 MiB
        qCInfo(hio::log::lcCore).noquote() << chunk;
    }
    const fs::path current = logging->currentFile();
    logging.reset();

    EXPECT_NE(first, current);
    EXPECT_TRUE(current.filename().string().ends_with(".part2.log")) << current;
    EXPECT_LE(fs::file_size(first), hio::log::maxFileBytes);
    EXPECT_GT(fs::file_size(first), hio::log::maxFileBytes - (std::uintmax_t{100} * 1024));
    EXPECT_GT(fs::file_size(current), 0U);
}

// Creates "<prefix>-20200101-000000-<index>.log"; a higher index is newer.
void createOldLogFile(const fs::path& dir, int index, std::uintmax_t size,
                      std::string_view prefix = "cli") {
    const fs::path path = dir / std::format("{}-20200101-000000-{}.log", prefix, index);
    {
        std::ofstream out(path);
        out << "old\n";
    }
    if (size > 0) {
        fs::resize_file(path, size); // sparse on most file systems
    }
    fs::last_write_time(path, fs::file_time_type::clock::now() - std::chrono::hours(1000 - index));
}

TEST(Logging, PrunesToMaxFileCount) {
    const QTemporaryDir dir;
    const fs::path root = tempPath(dir);
    for (int i = 0; i < 25; ++i) {
        createOldLogFile(root, i, 0);
    }
    {
        std::ofstream other(root / "notes.txt");
        other << "not a log file\n";
    }
    auto logging = initOrFail(AppKind::Cli, root);
    ASSERT_TRUE(logging);

    const std::vector<fs::path> files = ownLogFiles(root);
    EXPECT_EQ(files.size(), hio::log::cliRetention.maxFiles);
    EXPECT_TRUE(fs::exists(logging->currentFile()));
    EXPECT_TRUE(fs::exists(root / "notes.txt"));
    // The oldest ones (lowest index) were removed, the newest kept.
    EXPECT_FALSE(fs::exists(root / "cli-20200101-000000-0.log"));
    EXPECT_FALSE(fs::exists(root / "cli-20200101-000000-5.log"));
    EXPECT_TRUE(fs::exists(root / "cli-20200101-000000-6.log"));
    EXPECT_TRUE(fs::exists(root / "cli-20200101-000000-24.log"));
}

TEST(Logging, PrunesToMaxTotalSize) {
    const QTemporaryDir dir;
    const fs::path root = tempPath(dir);
    constexpr std::uintmax_t twentyMiB = 20ULL * 1024 * 1024;
    for (int i = 0; i < 5; ++i) {
        createOldLogFile(root, i, twentyMiB);
    }
    auto logging = initOrFail(AppKind::Cli, root);
    ASSERT_TRUE(logging);

    std::uintmax_t total = 0;
    for (const fs::path& file : ownLogFiles(root)) {
        total += fs::file_size(file);
    }
    EXPECT_LE(total, hio::log::cliRetention.maxTotalBytes);
    EXPECT_EQ(ownLogFiles(root).size(), 3U); // current + the 2 newest (40 MiB of 50 MiB)
    EXPECT_FALSE(fs::exists(root / "cli-20200101-000000-2.log"));
    EXPECT_TRUE(fs::exists(root / "cli-20200101-000000-3.log"));
}

TEST(Logging, PruningDeletesEverythingOlderThanTheFirstDeletedFile) {
    const QTemporaryDir dir;
    const fs::path root = tempPath(dir);
    createOldLogFile(root, 0, 1024);                // oldest, small
    createOldLogFile(root, 1, 60ULL * 1024 * 1024); // over the total limit on its own
    createOldLogFile(root, 2, 1024);                // newest, small
    auto logging = initOrFail(AppKind::Cli, root);
    ASSERT_TRUE(logging);

    EXPECT_TRUE(fs::exists(root / "cli-20200101-000000-2.log"));
    EXPECT_FALSE(fs::exists(root / "cli-20200101-000000-1.log"));
    EXPECT_FALSE(fs::exists(root / "cli-20200101-000000-0.log")); // older than a deleted file
}

TEST(Logging, CliPruningNeverTouchesGuiLogs) {
    const QTemporaryDir dir;
    const fs::path root = tempPath(dir);
    for (int i = 0; i < 25; ++i) {
        createOldLogFile(root, i, 0, "gui");
        createOldLogFile(root, i, 0, "cli");
    }
    auto logging = initOrFail(AppKind::Cli, root);
    ASSERT_TRUE(logging);

    for (int i = 0; i < 25; ++i) {
        EXPECT_TRUE(fs::exists(root / std::format("gui-20200101-000000-{}.log", i))) << i;
    }
    EXPECT_FALSE(fs::exists(root / "cli-20200101-000000-5.log"));
    EXPECT_TRUE(fs::exists(root / "cli-20200101-000000-6.log"));
}

TEST(Logging, GuiPruningUsesGuiLimitsAndNeverTouchesCliLogs) {
    const QTemporaryDir dir;
    const fs::path root = tempPath(dir);
    for (int i = 0; i < 15; ++i) {
        createOldLogFile(root, i, 0, "gui");
        createOldLogFile(root, i, 0, "cli");
    }
    auto logging = initOrFail(AppKind::Gui, root);
    ASSERT_TRUE(logging);

    for (int i = 0; i < 15; ++i) {
        EXPECT_TRUE(fs::exists(root / std::format("cli-20200101-000000-{}.log", i))) << i;
    }
    // Current file + the 9 newest old ones.
    const auto guiIndexKept = [&root](int i) {
        return fs::exists(root / std::format("gui-20200101-000000-{}.log", i));
    };
    EXPECT_FALSE(guiIndexKept(5));
    EXPECT_TRUE(guiIndexKept(6));
    EXPECT_TRUE(guiIndexKept(14));
}

TEST(Logging, NonAsciiLogDirectory) {
    const QTemporaryDir dir;
    const fs::path root =
        tempPath(dir) / fs::path(u"logy-\u017elu\u0165ou\u010dk\u00fd-\u65e5\u672c");
    auto logging = initOrFail(AppKind::Cli, root);
    ASSERT_TRUE(logging);
    const fs::path file = logging->currentFile();
    ASSERT_FALSE(file.empty());
    qCInfo(hio::log::lcCore) << "hello";
    logging.reset();
    EXPECT_NE(readFile(file).find("hello"), std::string::npos);
}

TEST(Logging, UnwritableDirectoryFallsBackToStderr) {
    const QTemporaryDir dir;
    const fs::path blocker = tempPath(dir) / "regular-file";
    {
        std::ofstream out(blocker);
        out << "x";
    }
    // A directory below a regular file can never be created (independent of user privileges).
    ::testing::internal::CaptureStderr();
    auto logging =
        Logging::init(AppKind::Cli, {.stderrLevel = Level::Warning, .logDir = blocker / "logs"});
    const std::string captured = ::testing::internal::GetCapturedStderr();
    ASSERT_TRUE(logging.has_value());
    EXPECT_TRUE((*logging)->currentFile().empty());
    EXPECT_NE(captured.find("Logging to stderr only"), std::string::npos) << captured;
    qCInfo(hio::log::lcCore) << "still works";
}

TEST(Logging, ConcurrentWritersProduceWholeLines) {
    const QTemporaryDir dir;
    auto logging = initOrFail(AppKind::Cli, tempPath(dir));
    ASSERT_TRUE(logging);
    const fs::path file = logging->currentFile();
    constexpr int threads = 8;
    constexpr int perThread = 500;
    {
        std::vector<std::jthread> writers;
        writers.reserve(threads);
        for (int t = 0; t < threads; ++t) {
            writers.emplace_back([t] {
                for (int i = 0; i < perThread; ++i) {
                    qCInfo(hio::log::lcCore).noquote()
                        << QStringLiteral("writer %1 message %2").arg(t).arg(i);
                }
            });
        }
    }
    logging.reset();

    int count = 0;
    const std::regex pattern(R"(^.* \[inf\] hio\.core \(0x[0-9a-f]+\) writer \d+ message \d+$)");
    for (const std::string& line : lines(readFile(file))) {
        if (line.find("writer ") != std::string::npos) {
            EXPECT_TRUE(std::regex_match(line, pattern)) << line;
            ++count;
        }
    }
    EXPECT_EQ(count, threads * perThread);
}

} // namespace
