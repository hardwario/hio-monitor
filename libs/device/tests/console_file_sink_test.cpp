#include "hio/core/error.h"
#include "hio/device/console_file_sink.h"
#include "hio/device/console_line.h"
#include "hio/device/probe_lock.h"
#include "hio/device/session_key.h"
#include "lock_holder.h"

#include <QDateTime>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QStringLiteral>
#include <QTemporaryDir>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hio::device::Channel;
using hio::device::ConsoleFileConfig;
using hio::device::ConsoleFileSink;
using hio::device::ConsoleLine;
using hio::device::Direction;

ConsoleLine line(Channel channel, Direction dir, std::string text) {
    ConsoleLine result;
    result.channel = channel;
    result.dir = dir;
    result.time = std::chrono::system_clock::now();
    result.text = std::move(text);
    return result;
}

std::string readAll(const fs::path& path) {
    std::ifstream input(path);
    std::stringstream content;
    content << input.rdbuf();
    return content.str();
}

// A sparse file of `size` bytes (fast, no real disk use).
void makeSparse(const fs::path& path, std::uintmax_t size) {
    QFile file(QString::fromStdString(path.string()));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_TRUE(file.resize(static_cast<qint64>(size)));
}

class Sink : public ::testing::Test {
protected:
    [[nodiscard]] fs::path dir() const { return fs::path(m_dir.path().toStdString()) / "console"; }

    [[nodiscard]] ConsoleFileConfig defaultConfig() const {
        return ConsoleFileConfig{.explicitFile = std::nullopt,
                                 .directory = dir(),
                                 .profileId = "chester",
                                 .targetId = "app",
                                 .lockDirectory = lockDir()};
    }

    [[nodiscard]] fs::path lockDir() const {
        return fs::path(m_dir.path().toStdString()) / "locks";
    }

    // 55 old console files of probe 1, "old-<i>" hours apart (old-0 the oldest, 100 h old).
    void makeOldFiles() const {
        fs::create_directories(dir());
        for (int i = 0; i < 55; ++i) {
            const fs::path old = oldFile(i);
            std::ofstream(old) << "x";
            fs::last_write_time(old,
                                fs::file_time_type::clock::now() - std::chrono::hours(100 - i));
        }
    }
    [[nodiscard]] fs::path oldFile(int i) const {
        return dir() / std::format("chester-app-20260101-0000{:02}-1.log", i);
    }

    [[nodiscard]] std::vector<fs::path> files() const {
        std::vector<fs::path> result;
        std::error_code ec;
        for (fs::directory_iterator it(dir(), ec), end; !ec && it != end; it.increment(ec)) {
            result.push_back(it->path());
        }
        return result;
    }

private:
    QTemporaryDir m_dir;
};

TEST_F(Sink, DefaultFileNameHeaderAndLineFormat) {
    auto sink = ConsoleFileSink::open(defaultConfig(), 801234567, "Console: CHESTER app",
                                      std::chrono::system_clock::now());
    ASSERT_TRUE(sink) << sink.error().detail;
    const fs::path path = (*sink)->currentFile();
    EXPECT_TRUE(std::regex_match(path.filename().string(),
                                 std::regex(R"(chester-app-\d{8}-\d{6}-801234567\.log)")))
        << path;
    ASSERT_TRUE((*sink)->write(line(Channel::Shell, Direction::Out, "uart:~$ version")));
    ASSERT_TRUE((*sink)->write(line(Channel::Log, Direction::Out, "<inf> app: x")));
    ASSERT_TRUE((*sink)->write(line(Channel::Shell, Direction::In, "version")));
    ASSERT_TRUE((*sink)->writeConnection("disconnected: The device has no power (0 mV).",
                                         std::chrono::system_clock::now()));
    ASSERT_TRUE((*sink)->flush());
    const std::string content = readAll(path);
    EXPECT_NE(content.find(std::string(80, '*') + "\n***"), std::string::npos) << content;
    EXPECT_NE(content.find("Console: CHESTER app"), std::string::npos);
    const std::regex stamp(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})");
    EXPECT_TRUE(std::regex_search(content, std::regex(R"(\.\d{3} > uart:~\$ version\n)")));
    EXPECT_TRUE(std::regex_search(content, std::regex(R"(\.\d{3} # <inf> app: x\n)")));
    EXPECT_TRUE(std::regex_search(content, std::regex(R"(\.\d{3} < version\n)")));
    EXPECT_TRUE(std::regex_search(content, std::regex(R"(\.\d{3} @ disconnected: The device)")));
    EXPECT_TRUE(std::regex_search(content, stamp));
}

TEST_F(Sink, DefaultModeRotatesAt10MiB) {
    auto sink =
        ConsoleFileSink::open(defaultConfig(), 1, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(sink);
    const fs::path first = (*sink)->currentFile();
    const std::string text(1000, 'x');
    for (std::size_t written = 0; written <= ConsoleFileSink::maxDefaultFileBytes;
         written += 1025) {
        ASSERT_TRUE((*sink)->write(line(Channel::Shell, Direction::Out, text)));
    }
    ASSERT_TRUE((*sink)->flush());
    EXPECT_NE((*sink)->currentFile(), first);
    EXPECT_NE((*sink)->currentFile().filename().string().find(".part2.log"), std::string::npos);
    EXPECT_LE(fs::file_size(first), ConsoleFileSink::maxDefaultFileBytes);
}

TEST_F(Sink, RetentionKeepsTheNewest50Files) {
    fs::create_directories(dir());
    for (int i = 0; i < 55; ++i) {
        const fs::path old = dir() / ("old-" + std::to_string(i) + ".log");
        std::ofstream(old) << "x";
        fs::last_write_time(old, fs::file_time_type::clock::now() - std::chrono::hours(100 - i));
    }
    std::ofstream(dir() / "history-chester-app.txt") << "version\n"; // not a console file
    auto sink =
        ConsoleFileSink::open(defaultConfig(), 1, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(sink);
    std::size_t logs = 0;
    for (const fs::path& path : files()) {
        logs += path.extension() == ".log" ? 1 : 0;
    }
    EXPECT_EQ(logs, ConsoleFileSink::maxDefaultFiles);
    EXPECT_FALSE(fs::exists(dir() / "old-0.log")); // the oldest went first
    EXPECT_TRUE(fs::exists(dir() / "old-54.log"));
    EXPECT_TRUE(fs::exists(dir() / "history-chester-app.txt"));
    EXPECT_TRUE(fs::exists((*sink)->currentFile()));
}

TEST_F(Sink, RetentionKeepsAtMost200MiB) {
    fs::create_directories(dir());
    for (int i = 0; i < 5; ++i) {
        const fs::path old = dir() / ("big-" + std::to_string(i) + ".log");
        makeSparse(old, 60ULL * 1024 * 1024);
        fs::last_write_time(old, fs::file_time_type::clock::now() - std::chrono::hours(10 - i));
    }
    auto sink =
        ConsoleFileSink::open(defaultConfig(), 1, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(sink);
    // 3 x 60 MiB + the new file fit into 200 MiB; the two oldest are gone.
    EXPECT_FALSE(fs::exists(dir() / "big-0.log"));
    EXPECT_FALSE(fs::exists(dir() / "big-1.log"));
    EXPECT_TRUE(fs::exists(dir() / "big-2.log"));
    EXPECT_TRUE(fs::exists(dir() / "big-4.log"));
}

// Review R45, R48: a long session of this process (it holds probe 777's lock, taken 300 h ago)
// rotated into three parts. Only its current part is live (the open-files list); its closed
// parts, written after the lock was taken, are pruned like any other old file, so a long session
// cannot keep its console files beyond the bounds.
TEST_F(Sink, RetentionPrunesTheClosedPartsOfALiveSession) {
    auto lock = hio::device::ProbeLock::acquire(777, lockDir());
    ASSERT_TRUE(lock) << lock.error().detail;
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(lockDir() / "probe-777.lock", now - std::chrono::hours(300));
    auto session =
        ConsoleFileSink::open(defaultConfig(), 777, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(session);
    const fs::path part1 = (*session)->currentFile();
    const std::string text(512ULL * 1024, 'x');
    for (int i = 0; i < 100 && (*session)->currentFile().filename().string().find(".part3.log") ==
                                   std::string::npos;
         ++i) {
        ASSERT_TRUE((*session)->write(line(Channel::Shell, Direction::Out, text)));
    }
    ASSERT_TRUE((*session)->flush());
    const fs::path part3 = (*session)->currentFile();
    ASSERT_NE(part3.filename().string().find(".part3.log"), std::string::npos) << part3;
    fs::path part2 = part1;
    part2.replace_extension(".part2.log");
    ASSERT_TRUE(fs::exists(part2)) << part2;
    fs::last_write_time(part1, now - std::chrono::hours(200)); // after the lock was taken
    fs::last_write_time(part2, now - std::chrono::hours(199));
    makeOldFiles();

    auto other =
        ConsoleFileSink::open(defaultConfig(), 1, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(other);
    EXPECT_FALSE(fs::exists(part1)); // closed parts of the live session: pruned
    EXPECT_FALSE(fs::exists(part2));
    EXPECT_TRUE(fs::exists(part3)); // its current part: kept
    // The new file, part 3 and 48 old files are the newest 50.
    EXPECT_TRUE(fs::exists(oldFile(7)));
    EXPECT_FALSE(fs::exists(oldFile(6)));
}

#ifndef _WIN32
// Review R45, R48: a session of another process (a real QLockFile written there, taken 300 h
// ago) that rotated into two parts: only the newest file of its probe written since the lock is
// live; its closed part and an older finished session's file are pruned.
TEST_F(Sink, RetentionKeepsOnlyTheNewestFileOfAProbeLockedByAnotherProcess) {
    makeOldFiles();
    fs::create_directories(lockDir());
    hio::device::test::LockHolder holder(
        QString::fromStdString((lockDir() / "probe-778.lock").string()));
    ASSERT_TRUE(holder.waitUntilLocked());
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(lockDir() / "probe-778.lock", now - std::chrono::hours(300));
    const fs::path closed = dir() / "chester-app-20260101-000000-778.log";
    const fs::path live = dir() / "chester-app-20260101-000000-778.part2.log";
    const fs::path finished = dir() / "chester-app-20251201-000000-778.log";
    std::ofstream(closed) << "x";
    std::ofstream(live) << "x";
    std::ofstream(finished) << "x";
    fs::last_write_time(closed, now - std::chrono::hours(200));
    fs::last_write_time(live, now - std::chrono::hours(150));
    fs::last_write_time(finished, now - std::chrono::hours(400));

    auto sink =
        ConsoleFileSink::open(defaultConfig(), 1, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(sink);
    EXPECT_TRUE(fs::exists(live)); // older than every old file, but live: kept beyond the bound
    EXPECT_FALSE(fs::exists(closed));
    EXPECT_FALSE(fs::exists(finished));
    EXPECT_FALSE(fs::exists(oldFile(0)));
    EXPECT_FALSE(fs::exists(oldFile(5)));
    EXPECT_TRUE(fs::exists(oldFile(6))); // the new file and 49 old files are the newest 50
    EXPECT_TRUE(fs::exists(oldFile(54)));

    holder.release(); // no live owner any more: the next prune deletes it
    auto next =
        ConsoleFileSink::open(defaultConfig(), 2, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(next);
    EXPECT_FALSE(fs::exists(live));
}
#endif

#ifndef _WIN32
// ADR-0128: a BLE session's newest file is live while another process holds its BleLock; the
// sanitised id in the file name finds the lock.
TEST_F(Sink, RetentionKeepsTheNewestFileOfABleDeviceLockedByAnotherProcess) {
    makeOldFiles();
    fs::create_directories(lockDir());
    hio::device::test::LockHolder holder(
        QString::fromStdString((lockDir() / "ble-F1-22-33-44-55-66.lock").string()));
    ASSERT_TRUE(holder.waitUntilLocked());
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(lockDir() / "ble-F1-22-33-44-55-66.lock", now - std::chrono::hours(300));
    const fs::path live = dir() / "chester-ble-20260101-000000-F1-22-33-44-55-66.log";
    const fs::path older = dir() / "chester-ble-20251201-000000-F1-22-33-44-55-66.log";
    std::ofstream(live) << "x";
    std::ofstream(older) << "x";
    fs::last_write_time(live, now - std::chrono::hours(150));
    fs::last_write_time(older, now - std::chrono::hours(160));

    auto sink =
        ConsoleFileSink::open(defaultConfig(), 1, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(sink);
    EXPECT_TRUE(fs::exists(live));
    EXPECT_FALSE(fs::exists(older)); // not the newest file of that device
    holder.release();
    auto next =
        ConsoleFileSink::open(defaultConfig(), 2, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(next);
    EXPECT_FALSE(fs::exists(live));
}
#endif

TEST_F(Sink, BleFileNameUsesTheSanitisedId) {
    const auto time = std::chrono::system_clock::now();
    auto sink = ConsoleFileSink::open(
        defaultConfig(), hio::device::SessionKey(hio::device::BleKey{.id = "F1:22:33:44:55:66"}),
        "Console: CHESTER, Bluetooth", time);
    ASSERT_TRUE(sink) << sink.error().detail;
    const std::string name = (*sink)->currentFile().filename().string();
    EXPECT_TRUE(
        std::regex_match(name, std::regex(R"(chester-ble-\d{8}-\d{6}-F1-22-33-44-55-66\.log)")))
        << name;
}

// Review R45: the files this process has open (another session's sink, here without any probe
// lock) are never deleted, even when they are the oldest.
TEST_F(Sink, RetentionKeepsTheFilesThisProcessHasOpen) {
    auto first =
        ConsoleFileSink::open(defaultConfig(), 900, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(first);
    const fs::path open = (*first)->currentFile();
    makeOldFiles();
    fs::last_write_time(open, fs::file_time_type::clock::now() - std::chrono::hours(200));

    auto second =
        ConsoleFileSink::open(defaultConfig(), 1, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(second);
    EXPECT_TRUE(fs::exists(open));
    EXPECT_FALSE(fs::exists(oldFile(0)));
    EXPECT_TRUE(fs::exists(oldFile(54)));

    first->reset(); // closed: no longer protected
    auto third =
        ConsoleFileSink::open(defaultConfig(), 2, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(third);
    EXPECT_FALSE(fs::exists(open));
}

TEST_F(Sink, ExplicitFileIsAppendedAndRotatedAt100MiB) {
    fs::create_directories(dir());
    const fs::path explicitFile = dir() / "my-console.txt";
    std::ofstream(explicitFile) << "previous run\n";
    ConsoleFileConfig config = defaultConfig();
    config.explicitFile = explicitFile;
    {
        auto sink = ConsoleFileSink::open(config, 1, "header", std::chrono::system_clock::now());
        ASSERT_TRUE(sink);
        EXPECT_EQ((*sink)->currentFile(), explicitFile);
    }
    EXPECT_EQ(readAll(explicitFile).rfind("previous run\n", 0), 0U); // appended

    makeSparse(explicitFile, ConsoleFileSink::maxExplicitFileBytes + 1);
    std::ofstream(dir() / "my-console.txt.1") << "older backup";
    auto sink = ConsoleFileSink::open(config, 1, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(sink);
    EXPECT_EQ(fs::file_size(dir() / "my-console.txt.1"), ConsoleFileSink::maxExplicitFileBytes + 1);
    EXPECT_LT(fs::file_size(explicitFile), 1024U);
}

// Review R26: when P cannot be renamed to P.1 (here P.1 is a non-empty directory), the file is
// not rotated again (one warning), keeps being appended to and is not written beyond 2 x 100 MiB.
TEST_F(Sink, FailedRotationOfTheExplicitFileWarnsOnceAndStaysBounded) {
    fs::create_directories(dir() / "my-console.txt.1");
    std::ofstream(dir() / "my-console.txt.1" / "keep") << "x";
    const fs::path explicitFile = dir() / "my-console.txt";
    constexpr std::uintmax_t headroom = 64ULL * 1024;
    makeSparse(explicitFile, ConsoleFileSink::maxExplicitFileBytesAfterFailedRotation - headroom);
    ConsoleFileConfig config = defaultConfig();
    config.explicitFile = explicitFile;
    auto sink = ConsoleFileSink::open(config, 1, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(sink) << sink.error().detail;
    const auto warning = (*sink)->takeWarning();
    ASSERT_TRUE(warning.has_value());
    EXPECT_EQ(warning.value_or(hio::Error{}).code, hio::ErrorCode::IoWriteFailed);
    EXPECT_TRUE(fs::is_directory(dir() / "my-console.txt.1"));
    std::uintmax_t size = fs::file_size(explicitFile);
    EXPECT_GT(size, ConsoleFileSink::maxExplicitFileBytesAfterFailedRotation - headroom);

    // Further flushes append without trying to rotate and without a new warning.
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE((*sink)->write(line(Channel::Shell, Direction::Out, "more")));
        ASSERT_TRUE((*sink)->flush());
        EXPECT_FALSE((*sink)->takeWarning().has_value());
    }
    EXPECT_GT(fs::file_size(explicitFile), size);

    // Beyond twice the limit the file is not written any more.
    const std::string longText(1000, 'x');
    bool failed = false;
    for (int i = 0; i < 200 && !failed; ++i) {
        ASSERT_TRUE((*sink)->write(line(Channel::Shell, Direction::Out, longText)));
        const auto status = (*sink)->flush();
        if (!status) {
            failed = true;
            EXPECT_EQ(status.error().code, hio::ErrorCode::IoWriteFailed);
        }
    }
    EXPECT_TRUE(failed);
    size = fs::file_size(explicitFile);
    EXPECT_LE(size, ConsoleFileSink::maxExplicitFileBytesAfterFailedRotation);
}

TEST_F(Sink, UnwritableLocationIsAnError) {
    fs::create_directories(dir());
    std::ofstream(dir() / "file") << "x";
    ConsoleFileConfig config = defaultConfig();
    config.explicitFile = dir() / "file" / "below-a-file.log";
    const auto sink = ConsoleFileSink::open(config, 1, "header", std::chrono::system_clock::now());
    ASSERT_FALSE(sink);
    EXPECT_EQ(sink.error().code, hio::ErrorCode::IoWriteFailed);
}

// ADR-0156 (M6): with consoleFileDir the generated name goes there, and the retention deletes only
// generated session files (the directory may hold other .log files of the user).
TEST_F(Sink, ConsoleFileDirKeepsForeignFiles) {
    const fs::path custom = dir().parent_path() / "custom";
    fs::create_directories(custom);
    for (int i = 0; i < 55; ++i) {
        const fs::path session = custom / std::format("chester-app-20260101-0000{:02}-1.log", i);
        std::ofstream(session) << "x";
        fs::last_write_time(session,
                            fs::file_time_type::clock::now() - std::chrono::hours(100 - i));
    }
    const fs::path foreign = custom / "build.log";
    std::ofstream(foreign) << "the user's file";
    fs::last_write_time(foreign, fs::file_time_type::clock::now() - std::chrono::hours(200));
    ConsoleFileConfig config = defaultConfig();
    config.consoleFileDir = custom;
    auto sink =
        ConsoleFileSink::open(config, 801234567, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(sink) << sink.error().detail;
    EXPECT_EQ((*sink)->currentFile().parent_path(), custom);
    EXPECT_TRUE(std::regex_match((*sink)->currentFile().filename().string(),
                                 std::regex(R"(chester-app-\d{8}-\d{6}-801234567\.log)")));
    EXPECT_TRUE(fs::exists(foreign)); // the oldest, but not a session file
    std::size_t sessionFiles = 0;
    std::error_code ec;
    for (fs::directory_iterator it(custom, ec), end; !ec && it != end; it.increment(ec)) {
        sessionFiles += it->path().filename().string().starts_with("chester-app-") ? 1 : 0;
    }
    EXPECT_EQ(sessionFiles, ConsoleFileSink::maxDefaultFiles);
    EXPECT_FALSE(fs::exists(dir())); // the default directory was not used
}

// Plan 0019: Qt converts local time through tzset() (glibc frees and re-duplicates its TZ copy on
// every call) under locks TSan cannot see, so the console file's timestamps on the session worker
// and a local-time conversion on another thread (the GUI's time column) were reported as a race.
// Under linux-tsan this fails without the race:tzset_internal entry in tools/sanitizers/tsan.supp;
// in the other presets it is a smoke test of a sink written on a worker thread.
TEST_F(Sink, LocalTimeOnAnotherThreadWhileTheWorkerWrites) {
    auto sink =
        ConsoleFileSink::open(defaultConfig(), 1, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(sink) << sink.error().detail;
    constexpr int iterations = 2000;
    bool written = true;
    qsizetype formatted = 0;
    {
        std::jthread worker([&sink, &written] {
            for (int i = 0; i < iterations && written; ++i) {
                written = static_cast<bool>(
                    (*sink)->write(line(Channel::Shell, Direction::Out, std::format("tz {}", i))));
            }
            written = written && static_cast<bool>((*sink)->flush());
        });
        for (int i = 0; i < iterations; ++i) {
            formatted += QDateTime::fromMSecsSinceEpoch(1'700'000'000'000 + (i * 1000LL))
                             .toString(QStringLiteral("HH:mm:ss.zzz"))
                             .size();
        }
    }
    EXPECT_TRUE(written);
    EXPECT_EQ(formatted, qsizetype{iterations} * 12);
    const std::string content = readAll((*sink)->currentFile());
    std::size_t lines = 0;
    for (std::size_t pos = content.find(" > tz "); pos != std::string::npos;
         pos = content.find(" > tz ", pos + 1)) {
        ++lines;
    }
    EXPECT_EQ(lines, std::size_t{iterations});
}

TEST(ConsoleFilePrefix, RtttPrefixes) {
    EXPECT_EQ(hio::device::consoleFilePrefix(line(Channel::Shell, Direction::Out, "")), " > ");
    EXPECT_EQ(hio::device::consoleFilePrefix(line(Channel::Log, Direction::Out, "")), " # ");
    EXPECT_EQ(hio::device::consoleFilePrefix(line(Channel::Shell, Direction::In, "")), " < ");
    EXPECT_EQ(hio::device::consoleFilePrefix(line(Channel::Shell, Direction::Marker, "")), " @ ");
}

} // namespace
