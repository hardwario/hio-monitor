// Crash handler (ADR-0123, plan/features/0015 B1): a child process crashes on purpose and the
// crash file and marker are checked; pruning and listing on prepared files.

#include "hio/core/crash_handler.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTemporaryDir>
#include <QtSystemDetection>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

fs::path toPath(const QString& text) { return {text.toStdU16String()}; }

struct Crash {
    bool crashed = false;
    std::vector<fs::path> files;
    QByteArray text;
};

Crash crashChild(const QTemporaryDir& dir, const QString& kind) {
    QProcess child;
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.start(QStringLiteral(HIO_CRASH_CHILD), {dir.path(), kind});
    EXPECT_TRUE(child.waitForFinished(30000)) << kind.toStdString();
    Crash crash;
    // The helper's own exit codes are 0 and 2-4 (crash_child.cpp).
    const int code = child.exitCode();
    crash.crashed = child.exitStatus() == QProcess::CrashExit ||
                    (code != 0 && code != 2 && code != 3 && code != 4);
    crash.files = hio::crash::recent(toPath(dir.path()), 10);
    if (!crash.files.empty()) {
        QFile file(QString::fromStdU16String(crash.files.front().u16string()));
        if (file.open(QIODevice::ReadOnly)) {
            crash.text = file.readAll();
        }
    }
    return crash;
}

class CrashHandler : public ::testing::Test {
protected:
    void SetUp() override {
#ifdef HIO_SANITIZER
        GTEST_SKIP() << "the crash handler is disabled in sanitizer builds";
#endif
        ASSERT_TRUE(m_dir.isValid());
    }
    [[nodiscard]] const QTemporaryDir& tempDir() const { return m_dir; }

private:
    QTemporaryDir m_dir;
};

#ifdef Q_OS_WIN

TEST_F(CrashHandler, AccessViolationWritesAMinidump) {
    for (const QString kind :
         {QStringLiteral("segv"), QStringLiteral("illegal"), QStringLiteral("div0")}) {
        const QTemporaryDir dir;
        const Crash crash = crashChild(dir, kind);
        EXPECT_TRUE(crash.crashed) << kind.toStdString();
        ASSERT_EQ(crash.files.size(), 1U) << kind.toStdString();
        EXPECT_EQ(crash.files.front().extension(), ".dmp");
        EXPECT_TRUE(crash.files.front().filename().string().starts_with("crash-crashtest-"));
        EXPECT_GT(fs::file_size(crash.files.front()), 0U);
        EXPECT_LE(fs::file_size(crash.files.front()), hio::crash::maxDumpBytes);
        EXPECT_TRUE(hio::crash::crashedLastRun(toPath(dir.path())));
    }
}

#else

TEST_F(CrashHandler, EverySignalWritesATextFileAndReRaises) {
    const std::vector<std::pair<QString, std::string>> kinds{{QStringLiteral("segv"), "(SIGSEGV)"},
                                                             {QStringLiteral("abort"), "(SIGABRT)"},
                                                             {QStringLiteral("bus"), "(SIGBUS)"},
                                                             {QStringLiteral("ill"), "(SIGILL)"},
                                                             {QStringLiteral("fpe"), "(SIGFPE)"}};
    for (const auto& [kind, signal] : kinds) {
        const QTemporaryDir dir;
        const Crash crash = crashChild(dir, kind);
        EXPECT_TRUE(crash.crashed) << kind.toStdString() << ": " << crash.text.toStdString();
        ASSERT_EQ(crash.files.size(), 1U) << kind.toStdString();
        const fs::path& file = crash.files.front();
        EXPECT_EQ(file.extension(), ".txt");
        EXPECT_TRUE(file.filename().string().starts_with("crash-crashtest-"));
        // crash-crashtest-YYYYMMDD-HHMMSS.txt
        EXPECT_EQ(file.filename().string().size(), std::string("crash-crashtest-").size() + 19);
        const std::string text = crash.text.toStdString();
        EXPECT_NE(text.find("application: crashtest"), std::string::npos) << text;
        EXPECT_NE(text.find("version: 1.2.3+test"), std::string::npos) << text;
        EXPECT_NE(text.find(signal), std::string::npos) << text;
        EXPECT_NE(text.find("backtrace:\n"), std::string::npos) << text;
        EXPECT_LE(text.size(), hio::crash::maxTextBytes);
        EXPECT_TRUE(hio::crash::crashedLastRun(toPath(dir.path())));
    }
}

#endif

TEST_F(CrashHandler, MarkerIsAcknowledged) {
    const fs::path dir = toPath(tempDir().path());
    EXPECT_FALSE(hio::crash::crashedLastRun(dir));
    std::ofstream(dir / hio::crash::markerFileName) << "crash-x.txt\n";
    EXPECT_TRUE(hio::crash::crashedLastRun(dir));
    hio::crash::acknowledge(dir);
    EXPECT_FALSE(hio::crash::crashedLastRun(dir));
    hio::crash::acknowledge(dir); // idempotent
}

TEST_F(CrashHandler, PruneKeepsTheNewestFiveAndDropsOversizeFiles) {
    const fs::path dir = toPath(tempDir().path());
    const auto now = fs::file_time_type::clock::now();
    for (int i = 0; i < 7; ++i) {
        const fs::path file = dir / ("crash-hio-2026010" + std::to_string(i) + "-000000.txt");
        std::ofstream(file) << "crash " << i << '\n';
        fs::last_write_time(file, now - std::chrono::hours(10 - i));
    }
    const fs::path big = dir / "crash-hio-20260109-000000.txt";
    std::ofstream(big) << std::string(hio::crash::maxTextBytes + 1, 'x');
    std::ofstream(dir / "unrelated.txt") << "kept\n";

    hio::crash::pruneOld(dir);
    const std::vector<fs::path> left = hio::crash::recent(dir, 10);
    ASSERT_EQ(left.size(), hio::crash::maxCrashFiles);
    EXPECT_EQ(left.front().filename(), "crash-hio-20260106-000000.txt"); // newest first
    EXPECT_EQ(left.back().filename(), "crash-hio-20260102-000000.txt");
    EXPECT_FALSE(fs::exists(big));
    EXPECT_TRUE(fs::exists(dir / "unrelated.txt"));
    EXPECT_EQ(hio::crash::recent(dir, hio::crash::reportCrashFiles).size(), 3U);
}

TEST_F(CrashHandler, CrashFileNames) {
    EXPECT_TRUE(hio::crash::isCrashFileName("crash-hio-monitor-20261026-101010.txt"));
    EXPECT_TRUE(hio::crash::isCrashFileName("crash-hio-20261026-101010.dmp"));
    EXPECT_FALSE(hio::crash::isCrashFileName("last-crash"));
    EXPECT_FALSE(hio::crash::isCrashFileName("crash.txt"));
    EXPECT_FALSE(hio::crash::isCrashFileName("gui-20261026.log"));
}

} // namespace
