#include "hwio/core/error.h"
#include "hwio/device/console_file_sink.h"
#include "hwio/device/console_line.h"
#include "hwio/device/firmware_identity.h"
#include "hwio/device/probe_lock.h"
#include "hwio/device/session_key.h"
#include "lock_holder.h"

#include <QDateTime>
#include <QFile>
#include <QIODevice>
#include <QLatin1Char>
#include <QMessageLogContext>
#include <QString>
#include <QStringLiteral>
#include <QTemporaryDir>
#include <QtLogging>
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
using hwio::device::Channel;
using hwio::device::ConsoleFileConfig;
using hwio::device::ConsoleFileSink;
using hwio::device::ConsoleLine;
using hwio::device::Direction;

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

// ADR-0186 (review M-4a/b): sensitive commands are written redacted; the file is owner-only.
TEST_F(Sink, SensitiveInputIsRedactedAndTheFileIsOwnerOnly) {
    auto sink = ConsoleFileSink::open(defaultConfig(), 801234567, "header",
                                      std::chrono::system_clock::now());
    ASSERT_TRUE(sink) << sink.error().detail;
    ASSERT_TRUE((*sink)->write(line(Channel::Shell, Direction::In, "lrw  config appkey 0011aabb")));
    ASSERT_TRUE((*sink)->write(line(Channel::Shell, Direction::In, "lte config password hunter2")));
    ASSERT_TRUE((*sink)->write(line(Channel::Shell, Direction::In, "lrw config show")));
    ASSERT_TRUE((*sink)->write(line(Channel::Shell, Direction::Out, "lrw config appkey 0011")));
    ASSERT_TRUE((*sink)->flush());
    const std::string content = readAll((*sink)->currentFile());
    EXPECT_NE(content.find(" < lrw config appkey <redacted>\n"), std::string::npos) << content;
    EXPECT_NE(content.find(" < lte config password <redacted>\n"), std::string::npos);
    EXPECT_EQ(content.find("hunter2"), std::string::npos);
    EXPECT_EQ(content.find("0011aabb"), std::string::npos);
    EXPECT_NE(content.find(" < lrw config show\n"), std::string::npos);
    // An echo of the command in the output is redacted too (review R4).
    EXPECT_NE(content.find(" > lrw config appkey <redacted>\n"), std::string::npos);
#ifndef _WIN32
    const fs::perms perms = fs::status((*sink)->currentFile()).permissions();
    EXPECT_EQ(perms & fs::perms::all, fs::perms::owner_read | fs::perms::owner_write);
#endif
}

// Review R4: a shell's echo of a sensitive command is redacted too (after its prompt).
TEST_F(Sink, EchoedSensitiveCommandIsRedacted) {
    auto sink = ConsoleFileSink::open(defaultConfig(), 801234567, "header",
                                      std::chrono::system_clock::now());
    ASSERT_TRUE(sink);
    ASSERT_TRUE(
        (*sink)->write(line(Channel::Shell, Direction::Out, "uart:~$ lte config password pw1")));
    ASSERT_TRUE((*sink)->write(line(Channel::Shell, Direction::Out, "lrw config appkey 00ff")));
    ASSERT_TRUE((*sink)->write(line(Channel::Shell, Direction::Out, "uart:~$ lrw config show")));
    ASSERT_TRUE((*sink)->flush());
    const std::string content = readAll((*sink)->currentFile());
    EXPECT_NE(content.find(" > uart:~$ lte config password <redacted>\n"), std::string::npos)
        << content;
    EXPECT_NE(content.find(" > lrw config appkey <redacted>\n"), std::string::npos);
    EXPECT_NE(content.find(" > uart:~$ lrw config show\n"), std::string::npos);
    EXPECT_EQ(content.find("pw1"), std::string::npos);
    EXPECT_EQ(content.find("00ff"), std::string::npos);
}

#ifndef _WIN32
// ADR-0186: an existing (explicit) file is tightened when it is opened again.
TEST_F(Sink, ExistingExplicitFileBecomesOwnerOnly) {
    fs::create_directories(dir());
    const fs::path explicitFile = dir() / "mine.txt";
    std::ofstream(explicitFile) << "old\n";
    fs::permissions(explicitFile, fs::perms::owner_read | fs::perms::owner_write |
                                      fs::perms::group_read | fs::perms::others_read);
    ConsoleFileConfig config = defaultConfig();
    config.explicitFile = explicitFile;
    auto sink = ConsoleFileSink::open(config, 1, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(sink);
    EXPECT_EQ(fs::status(explicitFile).permissions() & fs::perms::all,
              fs::perms::owner_read | fs::perms::owner_write);
}
#endif

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
    auto lock = hwio::device::ProbeLock::acquire(777, lockDir());
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
    hwio::device::test::LockHolder holder(
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
    hwio::device::test::LockHolder holder(
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
        defaultConfig(), hwio::device::SessionKey(hwio::device::BleKey{.id = "F1:22:33:44:55:66"}),
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
    EXPECT_EQ(warning.value_or(hwio::Error{}).code, hwio::ErrorCode::IoWriteFailed);
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
            EXPECT_EQ(status.error().code, hwio::ErrorCode::IoWriteFailed);
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
    EXPECT_EQ(sink.error().code, hwio::ErrorCode::IoWriteFailed);
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

// ---- plan 0028: firmware identity in the name (ADR-0200) -------------------------------------

using hwio::device::consoleFileSessionKey;
using hwio::device::FirmwareIdentity;
using hwio::device::ProbeKey;
using hwio::device::SessionKey;

SessionKey probe(std::uint32_t sn) { return SessionKey(ProbeKey{.sn = sn}); }
SessionKey bleKey(std::string id) { return SessionKey(hwio::device::BleKey{.id = std::move(id)}); }

TEST(ConsoleFileSessionKey, OldAndNewNames) {
    // Old names (kept in users' directories).
    EXPECT_EQ(consoleFileSessionKey("chester-app-20260101-000005-1.log"), probe(1));
    EXPECT_EQ(consoleFileSessionKey("chester-app-20260101-000000-778.part2.log"), probe(778));
    EXPECT_EQ(consoleFileSessionKey("chester-ble-20260101-000000-F1-22-33-44-55-66.log"),
              bleKey("F1-22-33-44-55-66"));
    EXPECT_EQ(consoleFileSessionKey("chester-ble-20260101-000000-F1-22-33-44-55-66.part3.log"),
              bleKey("F1-22-33-44-55-66"));
    // Plan 0028 names.
    EXPECT_EQ(consoleFileSessionKey("chester-app-clime-v3.0.0-20261102-101500-683012345.log"),
              probe(683012345));
    EXPECT_EQ(consoleFileSessionKey("chester-app-clime-v3.0.0-20261102-101500-683012345.part2.log"),
              probe(683012345));
    EXPECT_EQ(consoleFileSessionKey("chester-app-clime-v3.0.0-20261102-101500-683012345-2.log"),
              probe(683012345));
    EXPECT_EQ(
        consoleFileSessionKey("chester-app-clime-v3.0.0-20261102-101500-683012345-9.part4.log"),
        probe(683012345));
    EXPECT_EQ(
        consoleFileSessionKey("chester-ble-clime-v3.0.0-20261102-101500-F1-22-33-44-55-66.log"),
        bleKey("F1-22-33-44-55-66"));
    EXPECT_EQ(
        consoleFileSessionKey("chester-ble-clime-v3.0.0-20261102-101500-F1-22-33-44-55-66-2.log"),
        bleKey("F1-22-33-44-55-66"));
    // A version that looks like a stamp: J-Link parses from the end and is not misled.
    EXPECT_EQ(consoleFileSessionKey("chester-app-x-20250101-000000-20261102-101500-5.log"),
              probe(5));
    // A probe SN is all of the last token (a probe SN of 2 is not a collision suffix).
    EXPECT_EQ(consoleFileSessionKey("chester-app-20261102-101500-2.log"), probe(2));
    // Not session files.
    EXPECT_FALSE(consoleFileSessionKey("build.log"));
    EXPECT_FALSE(consoleFileSessionKey("chester-app-20261102-101500-x.log"));
    EXPECT_FALSE(consoleFileSessionKey("chester-app-20261102-101500-99999999999.log"));
    EXPECT_FALSE(consoleFileSessionKey("chester-app-clime-v3.0.0-20261102-101500-683.txt"));
    EXPECT_FALSE(consoleFileSessionKey("chester-ble-clime-v3.log"));
}

const FirmwareIdentity clime{
    .name = "CHESTER Clime", .version = "v3.0.0", .deviceSerial = "2159018247"};
const FirmwareIdentity push{
    .name = "hio-chester-push-fm", .version = "v4.0.1", .deviceSerial = "2159018247"};

std::string stampOf(const fs::path& file) {
    std::smatch match;
    const std::string name = file.filename().string();
    if (std::regex_search(name, match, std::regex(R"(\d{8}-\d{6})"))) {
        return match.str();
    }
    return {};
}

TEST_F(Sink, FirstIdentityRenamesTheFileAndWritesAMarker) {
    auto sink = ConsoleFileSink::open(defaultConfig(), 683012345, "Console: CHESTER app",
                                      std::chrono::system_clock::now());
    ASSERT_TRUE(sink);
    const fs::path before = (*sink)->currentFile();
    ASSERT_TRUE((*sink)->write(line(Channel::Shell, Direction::Out, "before")));
    ASSERT_TRUE((*sink)->adoptIdentity(std::nullopt, std::chrono::system_clock::now()));
    EXPECT_EQ((*sink)->currentFile(), before); // nullopt: no-op
    ASSERT_TRUE((*sink)->adoptIdentity(clime, std::chrono::system_clock::now()));
    const fs::path after = (*sink)->currentFile();
    EXPECT_EQ(after.filename().string(),
              std::format("chester-app-clime-v3.0.0-{}-683012345.log", stampOf(before)));
    EXPECT_FALSE(fs::exists(before));
    ASSERT_TRUE((*sink)->write(line(Channel::Shell, Direction::Out, "after")));
    ASSERT_TRUE((*sink)->adoptIdentity(clime, std::chrono::system_clock::now())); // the same
    EXPECT_EQ((*sink)->currentFile(), after);
    ASSERT_TRUE((*sink)->flush());
    const std::string content = readAll(after);
    EXPECT_NE(content.find("Console: CHESTER app"), std::string::npos);
    EXPECT_NE(content.find(" > before\n"), std::string::npos);
    EXPECT_TRUE(
        std::regex_search(content, std::regex(R"(\.\d{3} @ Firmware: CHESTER Clime v3\.0\.0\n)")));
    EXPECT_NE(content.find(" > after\n"), std::string::npos);
    EXPECT_EQ(content.find("2159018247"), std::string::npos); // the device serial is never written
    EXPECT_EQ(content.find("Firmware: CHESTER Clime"), content.rfind("Firmware: CHESTER Clime"));
    EXPECT_EQ(consoleFileSessionKey(after), probe(683012345));
}

TEST_F(Sink, FirstIdentityRenamesEveryPart) {
    auto sink =
        ConsoleFileSink::open(defaultConfig(), 7, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(sink);
    const fs::path first = (*sink)->currentFile();
    const std::string big(ConsoleFileSink::maxDefaultFileBytes / 2, 'x');
    for (int i = 0; i < 3; ++i) { // each line just over 5 MiB: every line starts a new part
        ASSERT_TRUE((*sink)->write(line(Channel::Shell, Direction::Out, big)));
        ASSERT_TRUE((*sink)->flush());
    }
    ASSERT_NE((*sink)->currentFile(), first);
    ASSERT_TRUE((*sink)->adoptIdentity(clime, std::chrono::system_clock::now()));
    const std::string stem = std::format("chester-app-clime-v3.0.0-{}-7", stampOf(first));
    EXPECT_TRUE(fs::exists(dir() / (stem + ".log")));
    EXPECT_TRUE(fs::exists(dir() / (stem + ".part2.log")));
    EXPECT_TRUE(fs::exists(dir() / (stem + ".part3.log")));
    EXPECT_EQ((*sink)->currentFile(), dir() / (stem + ".part3.log"));
    for (const fs::path& file : files()) {
        EXPECT_TRUE(file.filename().string().starts_with("chester-app-clime-")) << file;
    }
}

TEST_F(Sink, ATakenNameGetsACollisionSuffix) {
    const auto now = std::chrono::system_clock::now();
    auto sink = ConsoleFileSink::open(defaultConfig(), 7, "header", now);
    ASSERT_TRUE(sink);
    const std::string stem =
        std::format("chester-app-clime-v3.0.0-{}-7", stampOf((*sink)->currentFile()));
    std::ofstream(dir() / (stem + ".log")) << "an earlier session in the same second";
    ASSERT_TRUE((*sink)->adoptIdentity(clime, now));
    EXPECT_EQ((*sink)->currentFile(), dir() / (stem + "-2.log"));
    EXPECT_EQ(readAll(dir() / (stem + ".log")), "an earlier session in the same second");
    EXPECT_EQ(consoleFileSessionKey((*sink)->currentFile()), probe(7));
}

TEST_F(Sink, AnotherIdentityContinuesInANewFile) {
    const auto start = std::chrono::system_clock::now();
    auto sink = ConsoleFileSink::open(defaultConfig(), 7, "Console: CHESTER app", start);
    ASSERT_TRUE(sink);
    ASSERT_TRUE((*sink)->adoptIdentity(clime, start));
    const fs::path first = (*sink)->currentFile();
    const auto later = start + std::chrono::seconds(90);
    ASSERT_TRUE((*sink)->adoptIdentity(push, later));
    const fs::path second = (*sink)->currentFile();
    ASSERT_NE(second, first);
    EXPECT_TRUE(second.filename().string().starts_with("chester-app-hio-chester-push-fm-v4.0.1-"));
    EXPECT_NE(stampOf(second), stampOf(first)); // a new stamp
    ASSERT_TRUE((*sink)->flush());
    const std::string old = readAll(first);
    EXPECT_NE(old.find("@ Firmware changed to hio-chester-push-fm v4.0.1; continued in " +
                       second.filename().string()),
              std::string::npos)
        << old;
    const std::string next = readAll(second);
    EXPECT_NE(next.find("Console: CHESTER app"), std::string::npos);
    EXPECT_NE(next.find("@ Continues " + first.filename().string()), std::string::npos);
    EXPECT_NE(next.find("@ Firmware: hio-chester-push-fm v4.0.1"), std::string::npos);
    // The same firmware on another device (serial) is another identity too (D3).
    FirmwareIdentity otherDevice = push;
    otherDevice.deviceSerial = "2159018248";
    ASSERT_TRUE((*sink)->adoptIdentity(otherDevice, later + std::chrono::seconds(5)));
    EXPECT_NE((*sink)->currentFile(), second);
}

TEST_F(Sink, AnExplicitFileIsNeverRenamed) {
    fs::create_directories(dir());
    ConsoleFileConfig config = defaultConfig();
    config.explicitFile = dir() / "mine.log";
    auto sink = ConsoleFileSink::open(config, 7, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(sink);
    ASSERT_TRUE((*sink)->adoptIdentity(clime, std::chrono::system_clock::now()));
    EXPECT_EQ((*sink)->currentFile(), dir() / "mine.log");
    ASSERT_TRUE((*sink)->flush());
    EXPECT_EQ(readAll(dir() / "mine.log").find("Firmware:"), std::string::npos);
}

#ifndef _WIN32
// D11: a part that cannot be renamed keeps its name; the session keeps writing it.
TEST_F(Sink, ARenameFailureKeepsTheName) {
    auto sink =
        ConsoleFileSink::open(defaultConfig(), 7, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(sink);
    const fs::path before = (*sink)->currentFile();
    // A directory without write permission: the rename fails, appending to the file works.
    fs::permissions(dir(), fs::perms::owner_read | fs::perms::owner_exec);
    const auto status = (*sink)->adoptIdentity(clime, std::chrono::system_clock::now());
    fs::permissions(dir(), fs::perms::owner_all);
    ASSERT_TRUE(status) << status.error().detail;
    EXPECT_EQ((*sink)->currentFile(), before);
    ASSERT_TRUE((*sink)->write(line(Channel::Shell, Direction::Out, "still written")));
    ASSERT_TRUE((*sink)->flush());
    const std::string content = readAll(before);
    EXPECT_NE(content.find("@ Firmware: CHESTER Clime v3.0.0"), std::string::npos);
    EXPECT_NE(content.find("still written"), std::string::npos);
}

// The renamed file of another process's live session is still its newest file (R45).
TEST_F(Sink, RetentionKeepsARenamedLiveFileOfAnotherProcess) {
    makeOldFiles();
    fs::create_directories(lockDir());
    hwio::device::test::LockHolder holder(
        QString::fromStdString((lockDir() / "probe-778.lock").string()));
    ASSERT_TRUE(holder.waitUntilLocked());
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(lockDir() / "probe-778.lock", now - std::chrono::hours(300));
    const fs::path live = dir() / "chester-app-clime-v3.0.0-20260101-000000-778.log";
    std::ofstream(live) << "x";
    fs::last_write_time(live, now - std::chrono::hours(150));
    auto sink =
        ConsoleFileSink::open(defaultConfig(), 1, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(sink);
    EXPECT_TRUE(fs::exists(live));
    EXPECT_FALSE(fs::exists(oldFile(0)));
}
#endif

TEST_F(Sink, BleNamesCarryTheIdentityToo) {
    auto sink = ConsoleFileSink::open(defaultConfig(), bleKey("F1:22:33:44:55:66"), "header",
                                      std::chrono::system_clock::now());
    ASSERT_TRUE(sink);
    ASSERT_TRUE((*sink)->adoptIdentity(clime, std::chrono::system_clock::now()));
    const std::string name = (*sink)->currentFile().filename().string();
    EXPECT_TRUE(std::regex_match(
        name, std::regex(R"(chester-ble-clime-v3\.0\.0-\d{8}-\d{6}-F1-22-33-44-55-66\.log)")))
        << name;
    EXPECT_EQ(consoleFileSessionKey((*sink)->currentFile()), bleKey("F1-22-33-44-55-66"));
}

// Review round 1 (R1): an empty device serial is never a change; only another serial is another
// device ("Device changed", the serial is not written).
TEST_F(Sink, AnEmptySerialIsNoChangeAndAnotherSerialIsAnotherDevice) {
    const auto start = std::chrono::system_clock::now();
    auto sink = ConsoleFileSink::open(defaultConfig(), 7, "header", start);
    ASSERT_TRUE(sink);
    FirmwareIdentity noSerial = clime;
    noSerial.deviceSerial.clear();
    ASSERT_TRUE((*sink)->adoptIdentity(noSerial, start)); // e.g. the boot banner
    const fs::path first = (*sink)->currentFile();
    ASSERT_TRUE((*sink)->adoptIdentity(clime, start)); // the serial is learned: no new file
    EXPECT_EQ((*sink)->currentFile(), first);
    ASSERT_TRUE((*sink)->identity().has_value());
    EXPECT_EQ((*sink)->identity().value_or(FirmwareIdentity{}).deviceSerial, "2159018247");
    ASSERT_TRUE((*sink)->adoptIdentity(noSerial, start)); // the stored serial is kept
    EXPECT_EQ((*sink)->currentFile(), first);
    EXPECT_EQ((*sink)->identity().value_or(FirmwareIdentity{}).deviceSerial, "2159018247");

    FirmwareIdentity other = clime;
    other.deviceSerial = "2159018248";
    ASSERT_TRUE((*sink)->adoptIdentity(other, start + std::chrono::seconds(30)));
    const fs::path second = (*sink)->currentFile();
    EXPECT_NE(second, first);
    EXPECT_TRUE(second.filename().string().starts_with("chester-app-clime-v3.0.0-"));
    ASSERT_TRUE((*sink)->flush());
    const std::string old = readAll(first);
    EXPECT_NE(old.find("@ Device changed; continued in " + second.filename().string()),
              std::string::npos)
        << old;
    EXPECT_EQ(old.find("Firmware changed"), std::string::npos);
    EXPECT_EQ(old.find("2159018248"), std::string::npos); // never the serial

    // Another firmware with an empty serial keeps the stored one (no device change later).
    FirmwareIdentity pushNoSerial = push;
    pushNoSerial.deviceSerial.clear();
    ASSERT_TRUE((*sink)->adoptIdentity(pushNoSerial, start + std::chrono::seconds(60)));
    EXPECT_EQ((*sink)->identity().value_or(FirmwareIdentity{}).deviceSerial, "2159018248");
}

// Review round 1 (R3): the collision suffix is free for every part; a part that was pruned is
// skipped without the one-time rename-failure log.
TEST_F(Sink, CollisionSuffixIsFreeForEveryPartAndMissingPartsAreSkipped) {
    const auto now = std::chrono::system_clock::now();
    auto sink = ConsoleFileSink::open(defaultConfig(), 7, "header", now);
    ASSERT_TRUE(sink);
    const fs::path first = (*sink)->currentFile();
    const std::string big(ConsoleFileSink::maxDefaultFileBytes / 2, 'x');
    for (int i = 0; i < 2; ++i) { // each line just over 5 MiB: two parts
        ASSERT_TRUE((*sink)->write(line(Channel::Shell, Direction::Out, big)));
        ASSERT_TRUE((*sink)->flush());
    }
    const std::string stem = std::format("chester-app-clime-v3.0.0-{}-7", stampOf(first));
    std::ofstream(dir() / (stem + ".part2.log")) << "only the second part of the name is taken";
    fs::remove(first); // the closed first part was pruned meanwhile
    QString log;
    static QString* captured = nullptr;
    captured = &log;
    const QtMessageHandler previous =
        qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& text) {
            *captured += text + QLatin1Char('\n');
        });
    const auto status = (*sink)->adoptIdentity(clime, now);
    qInstallMessageHandler(previous);
    ASSERT_TRUE(status);
    EXPECT_EQ((*sink)->currentFile(), dir() / (stem + "-2.part2.log"));
    EXPECT_FALSE(log.contains(QStringLiteral("not renamed"))) << log.toStdString();
    EXPECT_EQ(readAll(dir() / (stem + ".part2.log")), "only the second part of the name is taken");
}

// Review round 1 (R2): the renamed live file is kept by the retention of another sink in this
// process, and released once its sink closes (no stale entry of the old or the new name).
TEST_F(Sink, RenamedLiveFileStaysProtectedInThisProcess) {
    auto first =
        ConsoleFileSink::open(defaultConfig(), 900, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(first);
    ASSERT_TRUE((*first)->adoptIdentity(clime, std::chrono::system_clock::now()));
    ASSERT_TRUE((*first)->flush()); // the marker: no later write changes the time below
    const fs::path renamed = (*first)->currentFile();
    makeOldFiles();
    fs::last_write_time(renamed, fs::file_time_type::clock::now() - std::chrono::hours(200));
    auto second =
        ConsoleFileSink::open(defaultConfig(), 1, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(second);
    EXPECT_TRUE(fs::exists(renamed));
    first->reset();
    auto third =
        ConsoleFileSink::open(defaultConfig(), 2, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(third);
    EXPECT_FALSE(fs::exists(renamed));
}

// Plan 0028 (U2, ADR-0186 amendment): the secrets `info show` prints are redacted in the file.
TEST_F(Sink, InfoShowSecretsAreRedacted) {
    auto sink =
        ConsoleFileSink::open(defaultConfig(), 7, "header", std::chrono::system_clock::now());
    ASSERT_TRUE(sink);
    for (const char* text :
         {"claim token: 0123456789abcdef", "ble passkey: 654321", "claim-token: fedcba9876543210",
          "\x1b[1;32mble passkey:\x1b[0m 111222", "uart:~$ claim token: aaaabbbb",
          "firmware name: CHESTER Clime", "serial number: 2159018247"}) {
        ASSERT_TRUE((*sink)->write(line(Channel::Shell, Direction::Out, text)));
    }
    ASSERT_TRUE((*sink)->flush());
    const std::string content = readAll((*sink)->currentFile());
    for (const char* secret :
         {"0123456789abcdef", "654321", "fedcba9876543210", "111222", "aaaabbbb"}) {
        EXPECT_EQ(content.find(secret), std::string::npos) << secret;
    }
    EXPECT_NE(content.find(" > claim token: <redacted>\n"), std::string::npos) << content;
    EXPECT_NE(content.find(" > ble passkey: <redacted>\n"), std::string::npos) << content;
    EXPECT_NE(content.find(" > claim-token: <redacted>\n"), std::string::npos) << content;
    EXPECT_NE(content.find(" > uart:~$ claim token: <redacted>\n"), std::string::npos) << content;
    EXPECT_NE(content.find(" > firmware name: CHESTER Clime\n"), std::string::npos);
    EXPECT_NE(content.find(" > serial number: 2159018247\n"), std::string::npos);
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
    EXPECT_EQ(hwio::device::consoleFilePrefix(line(Channel::Shell, Direction::Out, "")), " > ");
    EXPECT_EQ(hwio::device::consoleFilePrefix(line(Channel::Log, Direction::Out, "")), " # ");
    EXPECT_EQ(hwio::device::consoleFilePrefix(line(Channel::Shell, Direction::In, "")), " < ");
    EXPECT_EQ(hwio::device::consoleFilePrefix(line(Channel::Shell, Direction::Marker, "")), " @ ");
}

} // namespace
