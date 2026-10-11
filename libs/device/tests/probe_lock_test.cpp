#include "hio/core/error.h"
#include "hio/device/probe_lock.h"
#include "lock_holder.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QList>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QtTypes>

#include <gtest/gtest.h>

#ifdef _WIN32
#include <QtEnvironmentVariables>
#endif

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hio::ErrorCode;
using hio::device::ProbeLock;

class Lock : public ::testing::Test {
protected:
    [[nodiscard]] fs::path dir() const { return fs::path(m_dir.path().toStdString()) / "locks"; }

    [[nodiscard]] QString lockFile(std::uint32_t sn) const {
        return QString::fromStdString((dir() / ("probe-" + std::to_string(sn) + ".lock")).string());
    }

    // The lines of a lock file as QLockFile writes it on this host: PID, application name, host
    // name, machine id, boot id (Qt 6.8.3 qlockfile.cpp:377-385).
    [[nodiscard]] QList<QByteArray> qtLines(std::uint32_t sn) const {
        QByteArray content;
        {
            auto own = ProbeLock::acquire(sn, dir());
            EXPECT_TRUE(own);
            QFile file(lockFile(sn));
            EXPECT_TRUE(file.open(QIODevice::ReadOnly));
            content = file.readAll();
        }
        return content.split('\n');
    }

    // Lock file content as QLockFile writes it, with another PID and application name.
    [[nodiscard]] QByteArray contentFor(std::uint32_t sn, qint64 pid, const QByteArray& app) const {
        QList<QByteArray> lines = qtLines(sn);
        lines[0] = QByteArray::number(pid);
        lines[1] = app;
        return lines.join('\n');
    }

    void writeLock(std::uint32_t sn, const QByteArray& content) const {
        QFile file(lockFile(sn));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }

private:
    QTemporaryDir m_dir;
};

TEST_F(Lock, AcquireAndRelease) {
    {
        auto lock = ProbeLock::acquire(42, dir());
        ASSERT_TRUE(lock) << lock.error().detail;
        EXPECT_EQ(lock->serialNumber(), 42U);
        EXPECT_TRUE(QFile::exists(lockFile(42)));
        const auto owner = ProbeLock::owner(42, dir());
        ASSERT_TRUE(owner.has_value());
        EXPECT_EQ(owner.value_or(hio::device::ProbeLockOwner{}).pid,
                  QCoreApplication::applicationPid());
    }
    EXPECT_FALSE(QFile::exists(lockFile(42)));
    EXPECT_FALSE(ProbeLock::owner(42, dir()).has_value());
}

TEST_F(Lock, SecondAcquireInTheSameProcessFails) {
    auto first = ProbeLock::acquire(43, dir());
    ASSERT_TRUE(first);
    const auto second = ProbeLock::acquire(43, dir());
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error().code, ErrorCode::ProbeOpenFailed);
    EXPECT_EQ(second.error().message, "The J-Link 43 is already used by this application.");
    EXPECT_TRUE(ProbeLock::acquire(44, dir())); // another probe is fine
}

TEST_F(Lock, MoveKeepsTheLock) {
    auto lock = ProbeLock::acquire(45, dir());
    ASSERT_TRUE(lock);
    ProbeLock moved = std::move(*lock);
    EXPECT_TRUE(QFile::exists(lockFile(45)));
    moved.release();
    EXPECT_FALSE(QFile::exists(lockFile(45)));
}

#ifndef _WIN32
TEST_F(Lock, HeldByAnotherProcessNamesTheOwner) {
    QProcess other;
    other.start(QStringLiteral("sleep"), {QStringLiteral("30")});
    ASSERT_TRUE(other.waitForStarted(5000));
    writeLock(46, contentFor(46, other.processId(), "sleep"));

    const auto lock = ProbeLock::acquire(46, dir());
    ASSERT_FALSE(lock);
    EXPECT_EQ(lock.error().code, ErrorCode::ProbeInUseByOtherProcess);
    EXPECT_EQ(lock.error().message, "The J-Link 46 is used by another program (sleep, PID " +
                                        std::to_string(other.processId()) + ").");
    const auto owner = ProbeLock::owner(46, dir());
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(owner.value_or(hio::device::ProbeLockOwner{}).app, "sleep");
    EXPECT_EQ(owner.value_or(hio::device::ProbeLockOwner{}).pid, other.processId());

    other.kill();
    ASSERT_TRUE(other.waitForFinished(5000));
}

TEST_F(Lock, StaleLockOfADeadProcessIsTakenOver) {
    QProcess gone;
    gone.start(QStringLiteral("sleep"), {QStringLiteral("0.1")});
    ASSERT_TRUE(gone.waitForStarted(5000));
    const qint64 pid = gone.processId(); // 0 once the process has finished
    ASSERT_GT(pid, 0);
    ASSERT_TRUE(gone.waitForFinished(5000));
    writeLock(47, contentFor(47, pid, "sleep"));
    EXPECT_FALSE(ProbeLock::owner(47, dir()).has_value());
    const auto lock = ProbeLock::acquire(47, dir());
    EXPECT_TRUE(lock) << lock.error().message;
}

// Review R37: owner() uses QLockFile's stale rules, like acquire(): a live PID that runs another
// program than the one recorded (a reused PID) is no owner, and acquire() takes the lock over.
TEST_F(Lock, LiveProcessWithAnotherAppNameIsNoOwner) {
    QProcess other;
    other.start(QStringLiteral("sleep"), {QStringLiteral("30")});
    ASSERT_TRUE(other.waitForStarted(5000));
    writeLock(51, contentFor(51, other.processId(), "not-the-recorded-app"));
    EXPECT_FALSE(ProbeLock::owner(51, dir()).has_value());
    const auto lock = ProbeLock::acquire(51, dir());
    EXPECT_TRUE(lock) << lock.error().message;
    other.kill();
    ASSERT_TRUE(other.waitForFinished(5000));
}
#endif

// Review R37: a lock file with our own PID for a probe this process does not hold is no owner
// (the R15 rule of acquire()); once this process holds the probe, it is the owner.
TEST_F(Lock, OwnPidNotHeldIsNoOwner) {
    writeLock(52, contentFor(52, QCoreApplication::applicationPid(), "hio_device_tests"));
    EXPECT_FALSE(ProbeLock::owner(52, dir()).has_value());
    auto lock = ProbeLock::acquire(52, dir());
    ASSERT_TRUE(lock) << lock.error().message;
    const auto owner = ProbeLock::owner(52, dir());
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(owner.value_or(hio::device::ProbeLockOwner{}).pid,
              QCoreApplication::applicationPid());
}

// Review R15: a lock file naming our own PID for a probe this process does not hold is a stale
// lock of a dead process whose PID we reused: it is taken over. A probe we hold stays refused.
TEST_F(Lock, OwnPidLockNotHeldInProcessIsTakenOver) {
    writeLock(49, contentFor(49, QCoreApplication::applicationPid(), "hio_device_tests"));
    auto lock = ProbeLock::acquire(49, dir());
    ASSERT_TRUE(lock) << lock.error().message;
    const auto again = ProbeLock::acquire(49, dir());
    ASSERT_FALSE(again);
    EXPECT_EQ(again.error().code, ErrorCode::ProbeOpenFailed);
    EXPECT_EQ(again.error().message, "The J-Link 49 is already used by this application.");
}

// Review R20: concurrent acquire / release of the same probe in one process. The registry of held
// probes is updated together with the file lock, so an acquire never sees "our PID, not held" for
// a lock that is being taken or released: never two holders, no lock removed under its holder.
TEST_F(Lock, ConcurrentAcquireAndReleaseOfTheSameProbe) {
    constexpr std::uint32_t sn = 50;
    constexpr int threadCount = 6;
    constexpr int iterations = 300;
    std::atomic<int> holders{0};
    std::atomic<int> maxHolders{0};
    std::atomic<int> acquired{0};
    std::atomic<int> unexpectedErrors{0};
    std::atomic<int> lostFiles{0};
    {
        std::vector<std::jthread> threads;
        threads.reserve(threadCount);
        for (int t = 0; t < threadCount; ++t) {
            threads.emplace_back([&] {
                for (int i = 0; i < iterations; ++i) {
                    auto lock = ProbeLock::acquire(sn, dir());
                    if (!lock) {
                        if (lock.error().code != ErrorCode::ProbeOpenFailed) {
                            unexpectedErrors.fetch_add(1);
                        }
                        continue;
                    }
                    const int now = holders.fetch_add(1) + 1;
                    int seen = maxHolders.load();
                    while (now > seen && !maxHolders.compare_exchange_weak(seen, now)) {
                    }
                    acquired.fetch_add(1);
                    if (!QFile::exists(lockFile(sn))) {
                        lostFiles.fetch_add(1); // removed under its holder (stolen)
                    }
                    std::this_thread::yield();
                    holders.fetch_sub(1);
                    lock->release();
                }
            });
        }
    }
    EXPECT_EQ(maxHolders.load(), 1);
    EXPECT_EQ(unexpectedErrors.load(), 0);
    EXPECT_EQ(lostFiles.load(), 0);
    EXPECT_GT(acquired.load(), 0);
    EXPECT_FALSE(QFile::exists(lockFile(sn)));
    EXPECT_TRUE(ProbeLock::acquire(sn, dir())); // nothing left behind
}

// The application name QLockFile records for this test binary (Qt 6.8.3 processNameByPid():
// Windows the module name without extension, Linux the file name of /proc/<pid>/exe).
QString recordedAppName() {
    const QFileInfo exe(QCoreApplication::applicationFilePath());
#ifdef _WIN32
    return exe.completeBaseName();
#else
    return QFileInfo(exe.canonicalFilePath()).fileName();
#endif
}

// Review R41: a lock that QLockFile took in another process (with the application and host
// names Qt records on this OS) has an owner, and acquire() refuses it; once released, neither.
// On Windows the old code compared "hio_device_tests.exe" and QSysInfo::machineHostName() with
// Qt's "hio_device_tests" and COMPUTERNAME, and never found an owner.
TEST_F(Lock, LockOfAnotherProcessThroughQLockFileHasAnOwner) {
    fs::create_directories(dir());
    hio::device::test::LockHolder holder(lockFile(60));
    ASSERT_TRUE(holder.waitUntilLocked());
    const auto owner = ProbeLock::owner(60, dir());
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(owner.value_or(hio::device::ProbeLockOwner{}).pid, holder.pid());
#ifndef __APPLE__ // macOS records proc_name(), which may be shortened
    EXPECT_EQ(owner.value_or(hio::device::ProbeLockOwner{}).app, recordedAppName().toStdString());
#endif
    EXPECT_TRUE(owner.value_or(hio::device::ProbeLockOwner{}).lockedAt.has_value());
    const auto lock = ProbeLock::acquire(60, dir());
    ASSERT_FALSE(lock);
    EXPECT_EQ(lock.error().code, ErrorCode::ProbeInUseByOtherProcess);
    holder.release();
    EXPECT_FALSE(ProbeLock::owner(60, dir()).has_value());
    EXPECT_TRUE(ProbeLock::acquire(60, dir()));
}

// Review R41: a lock file that exists but cannot be read is not stale for QLockFile (no age
// rule): acquire() fails with an unknown owner, and owner() reports the same.
TEST_F(Lock, UnreadableLockFileIsAnUnknownOwner) {
    fs::create_directories(dir());
    writeLock(64, QByteArray());
    const auto owner = ProbeLock::owner(64, dir());
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(owner.value_or(hio::device::ProbeLockOwner{}).app, "?");
    hio::device::ProbeLockOwner none;
    none.pid = 1;
    EXPECT_EQ(owner.value_or(none).pid, 0);
    const auto lock = ProbeLock::acquire(64, dir());
    ASSERT_FALSE(lock);
    EXPECT_EQ(lock.error().code, ErrorCode::ProbeInUseByOtherProcess);
}

#ifndef _WIN32
// Review R41: QLockFile's boot-id rule - on this host, a lock written in another boot is stale
// even when its PID runs the recorded program now.
TEST_F(Lock, LockOfAnotherBootIsNoOwner) {
    QProcess other;
    other.start(QStringLiteral("sleep"), {QStringLiteral("30")});
    ASSERT_TRUE(other.waitForStarted(5000));
    QList<QByteArray> lines = qtLines(65);
    lines[0] = QByteArray::number(other.processId());
    lines[1] = "sleep";
    writeLock(65, lines.join('\n'));
    ASSERT_TRUE(ProbeLock::owner(65, dir()).has_value()); // the same lock of this boot
    lines[4] = "00000000-0000-0000-0000-000000000000";
    writeLock(65, lines.join('\n'));
    EXPECT_FALSE(ProbeLock::owner(65, dir()).has_value());
    EXPECT_TRUE(ProbeLock::acquire(65, dir()));
    other.kill();
    ASSERT_TRUE(other.waitForFinished(5000));
}

// Review R41: QLockFile's host rule - the machine id decides whether a lock is from this host
// when both sides know it, not the host name: a dead PID of another machine is an owner (it may
// run there), a dead PID of this machine under another host name is stale.
TEST_F(Lock, MachineIdDecidesTheHost) {
    if (QSysInfo::machineUniqueId().isEmpty()) {
        GTEST_SKIP() << "this host has no machine id";
    }
    QProcess gone;
    gone.start(QStringLiteral("sleep"), {QStringLiteral("0.1")});
    ASSERT_TRUE(gone.waitForStarted(5000));
    const qint64 pid = gone.processId();
    ASSERT_TRUE(gone.waitForFinished(5000));
    QList<QByteArray> lines = qtLines(66);
    lines[0] = QByteArray::number(pid);
    lines[1] = "sleep";
    lines[3] = "another-machine-id";
    writeLock(66, lines.join('\n')); // our host name, another machine id
    EXPECT_TRUE(ProbeLock::owner(66, dir()).has_value());
    const auto refused = ProbeLock::acquire(66, dir());
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, ErrorCode::ProbeInUseByOtherProcess);

    lines = qtLines(67);
    lines[0] = QByteArray::number(pid);
    lines[1] = "sleep";
    lines[2] = "another-host-name";
    writeLock(67, lines.join('\n')); // another host name, our machine id
    EXPECT_FALSE(ProbeLock::owner(67, dir()).has_value());
    EXPECT_TRUE(ProbeLock::acquire(67, dir()));
}

// Review R41: QLockFile takes over an apparently stale lock only when it can remove it, which
// fails while the owner holds its flock. A live holder whose recorded name does not match (here:
// rewritten after it locked) therefore stays the owner, as acquire() sees it.
TEST_F(Lock, ApparentlyStaleLockStillHeldByItsOwnerHasAnOwner) {
    fs::create_directories(dir());
    hio::device::test::LockHolder holder(lockFile(63));
    ASSERT_TRUE(holder.waitUntilLocked());
    QFile file(lockFile(63));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QList<QByteArray> lines = file.readAll().split('\n');
    file.close();
    lines[1] = "not-the-recorded-app";
    writeLock(63, lines.join('\n')); // same file (inode), the holder keeps its flock
    const auto owner = ProbeLock::owner(63, dir());
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(owner.value_or(hio::device::ProbeLockOwner{}).pid, holder.pid());
    const auto lock = ProbeLock::acquire(63, dir());
    ASSERT_FALSE(lock);
    EXPECT_EQ(lock.error().code, ErrorCode::ProbeInUseByOtherProcess);
}
#endif

#ifdef _WIN32
// Review R41 (Windows CI): what QLockFile records on Windows - the application name without
// directory and extension and %COMPUTERNAME% as the host name.
TEST_F(Lock, WindowsRecordsTheAppNameWithoutExtensionAndTheComputerName) {
    fs::create_directories(dir());
    hio::device::test::LockHolder holder(lockFile(61));
    ASSERT_TRUE(holder.waitUntilLocked());
    QFile file(lockFile(61));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QList<QByteArray> lines = file.readAll().split('\n');
    ASSERT_GE(lines.size(), 3);
    EXPECT_EQ(lines[0].toLongLong(), holder.pid());
    EXPECT_EQ(QString::fromUtf8(lines[1]), recordedAppName());
    EXPECT_FALSE(lines[1].endsWith(".exe"));
    EXPECT_EQ(QString::fromUtf8(lines[2]), qEnvironmentVariable("COMPUTERNAME"));
    const auto owner = ProbeLock::owner(61, dir());
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(owner.value_or(hio::device::ProbeLockOwner{}).app, recordedAppName().toStdString());
}

// Review R41 (Windows CI): Qt compares the running process's name without extension with the
// recorded one, as it is: a lock file (not held open) naming the live helper's PID is an owner
// with "hio_device_tests" and stale with "hio_device_tests.exe" (a reused PID for Qt).
TEST_F(Lock, WindowsComparesTheAppNameWithoutExtension) {
    fs::create_directories(dir());
    hio::device::test::LockHolder holder(lockFile(62));
    ASSERT_TRUE(holder.waitUntilLocked());
    writeLock(68, contentFor(68, holder.pid(), recordedAppName().toUtf8()));
    EXPECT_TRUE(ProbeLock::owner(68, dir()).has_value());
    writeLock(68, contentFor(68, holder.pid(), recordedAppName().toUtf8() + ".exe"));
    EXPECT_FALSE(ProbeLock::owner(68, dir()).has_value());
    EXPECT_TRUE(ProbeLock::acquire(68, dir()));
}
#endif

TEST_F(Lock, UncreatableDirectoryIsAWriteError) {
    fs::create_directories(dir());
    QFile blocker(QString::fromStdString((dir() / "file").string()));
    ASSERT_TRUE(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    const auto lock = ProbeLock::acquire(48, dir() / "file" / "sub");
    ASSERT_FALSE(lock);
    EXPECT_EQ(lock.error().code, ErrorCode::IoWriteFailed);
}

TEST_F(Lock, DefaultDirectory) { EXPECT_EQ(ProbeLock::defaultDirectory().filename(), "locks"); }

} // namespace
