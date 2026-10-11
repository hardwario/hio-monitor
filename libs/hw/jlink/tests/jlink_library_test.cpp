// JLinkLibrary against the test-only fake library (tests/fake_jlink): real copy + load + symbol
// resolution + version check + out handlers (plan/features/0002, phases A1 and E2).

#include "hio/core/error.h"
#include "hio/hw/jlink/jlink_library.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/probe_enumerator.h"
#include "hio/hw/jlink/types.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QList>
#include <QLockFile>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#include <QtEnvironmentVariables>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

namespace fs = std::filesystem;
using hio::ErrorCode;
using hio::jlink::JLinkLibrary;
using hio::jlink::LibraryLocation;

class FakeLibrary : public ::testing::Test {
protected:
    void SetUp() override { useScenario(R"({"probes": []})"); }
    void TearDown() override { qunsetenv("HIO_FAKE_JLINK_SCENARIO"); }

    void useScenario(const char* json) {
        const QString path = m_dir.filePath(QStringLiteral("scenario.json"));
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(json);
        file.close();
        qputenv("HIO_FAKE_JLINK_SCENARIO", path.toUtf8());
    }

    [[nodiscard]] fs::path instanceRoot() const {
        return fs::path(m_dir.path().toStdString()) / "instances";
    }

    [[nodiscard]] static LibraryLocation fakeLocation() {
        return LibraryLocation{.path = fs::path(HIO_FAKE_JLINK_LIBRARY)};
    }

    [[nodiscard]] std::size_t instanceCount() const {
        std::size_t count = 0;
        std::error_code ec;
        for (fs::directory_iterator it(instanceRoot(), ec), end; !ec && it != end;
             it.increment(ec)) {
            ++count;
        }
        return count;
    }

private:
    QTemporaryDir m_dir;
};

TEST_F(FakeLibrary, LoadsACopyAndRemovesItAgain) {
    fs::path copy;
    {
        auto library = JLinkLibrary::loadCopy(fakeLocation(), instanceRoot());
        ASSERT_TRUE(library) << library.error().detail;
        copy = (*library)->copyPath();
        EXPECT_TRUE(fs::exists(copy));
        EXPECT_NE(copy, (*library)->sourcePath());
        EXPECT_EQ(copy.filename(), fs::path(HIO_FAKE_JLINK_LIBRARY).filename());
        EXPECT_EQ((*library)->libraryVersion(), 81000U);
        EXPECT_EQ(instanceCount(), 1U);
    }
    EXPECT_FALSE(fs::exists(copy));
    EXPECT_EQ(instanceCount(), 0U);
}

TEST_F(FakeLibrary, CopiesHaveIndependentState) {
    useScenario(R"({"probes": [{"serialNumber": 1}, {"serialNumber": 2}]})");
    auto first = JLinkLibrary::loadCopy(fakeLocation(), instanceRoot());
    auto second = JLinkLibrary::loadCopy(fakeLocation(), instanceRoot());
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_NE((*first)->copyPath(), (*second)->copyPath());
    ASSERT_EQ((*first)->selectBySerialNumber(1), 0);
    ASSERT_EQ((*second)->selectBySerialNumber(2), 0);
    EXPECT_FALSE((*first)->open().has_value());
    EXPECT_FALSE((*second)->open().has_value());
    EXPECT_EQ((*first)->serialNumber(), 1U);
    EXPECT_EQ((*second)->serialNumber(), 2U);
    (*first)->close();
    EXPECT_FALSE((*first)->isOpen());
    EXPECT_TRUE((*second)->isOpen()); // a separate instance of the library
}

TEST_F(FakeLibrary, ListsProbes) {
    useScenario(R"({"probes": [
        {"serialNumber": 801000001, "product": "Test Probe"},
        {"serialNumber": 801000002, "product": "Test Probe IP", "connection": "ip",
         "listFirmware": "J-Link V4 compiled Oct  1 2024 10:00:00"}]})");
    const auto enumeration = hio::jlink::enumerateProbes(fakeLocation(), instanceRoot());
    ASSERT_TRUE(enumeration) << enumeration.error().detail;
    EXPECT_EQ(enumeration->libraryVersion, 81000U);
    ASSERT_EQ(enumeration->probes.size(), 2U);
    EXPECT_EQ(enumeration->probes[0].serialNumber, 801000001U);
    EXPECT_EQ(enumeration->probes[0].product, "Test Probe");
    EXPECT_EQ(enumeration->probes[0].connection, hio::jlink::Connection::Usb);
    EXPECT_EQ(enumeration->probes[1].connection, hio::jlink::Connection::Ip);
    EXPECT_EQ(enumeration->probes[1].firmware, "J-Link V4 compiled Oct  1 2024 10:00:00");
    EXPECT_EQ(instanceCount(), 0U); // the short-lived copy is gone
}

TEST_F(FakeLibrary, TooOldLibraryIsRejected) {
    useScenario(R"({"libraryVersion": 79411})");
    const auto library = JLinkLibrary::loadCopy(fakeLocation(), instanceRoot());
    ASSERT_FALSE(library);
    EXPECT_EQ(library.error().code, ErrorCode::DependencyJLinkTooOld);
    EXPECT_EQ(library.error().message, "J-Link Software 7.94k is too old.");
    EXPECT_NE(library.error().detail.find("8.00"), std::string::npos);
    EXPECT_EQ(library.error().wizard, hio::WizardId::FirstRunSetup);
    EXPECT_EQ(instanceCount(), 0U);
}

TEST_F(FakeLibrary, MissingFileIsNotFound) {
    const auto library =
        JLinkLibrary::loadCopy(LibraryLocation{.path = instanceRoot() / "nope.so"}, instanceRoot());
    ASSERT_FALSE(library);
    EXPECT_EQ(library.error().code, ErrorCode::DependencyJLinkNotFound);
}

TEST_F(FakeLibrary, NonLibraryFileFailsToLoad) {
    fs::create_directories(instanceRoot());
    const fs::path bogus = instanceRoot().parent_path() / "libjlinkarm.so.8.10.1";
    std::ofstream(bogus) << "this is not a shared library";
    const auto library = JLinkLibrary::loadCopy(LibraryLocation{.path = bogus}, instanceRoot());
    ASSERT_FALSE(library);
    EXPECT_EQ(library.error().code, ErrorCode::DependencyJLinkLoadFailed);
    EXPECT_EQ(instanceCount(), 0U);
}

TEST_F(FakeLibrary, MissingSymbolsAreNamed) {
    const auto library = JLinkLibrary::loadCopy(
        LibraryLocation{.path = fs::path(HIO_FAKE_JLINK_INCOMPLETE_LIBRARY)}, instanceRoot());
    ASSERT_FALSE(library);
    EXPECT_EQ(library.error().code, ErrorCode::DependencyJLinkLoadFailed);
    EXPECT_NE(library.error().detail.find("JLINKARM_Open"), std::string::npos)
        << library.error().detail;
    EXPECT_NE(library.error().detail.find("JLINK_RTTERMINAL_Read"), std::string::npos);
    EXPECT_EQ(library.error().detail.find("JLINKARM_GetDLLVersion"), std::string::npos);
}

TEST_F(FakeLibrary, ErrorOutputReachesTheNextErrorDetail) {
    useScenario(R"({"probes": [{"serialNumber": 7, "openError": "USB communication failed"}]})");
    auto library = JLinkLibrary::loadCopy(fakeLocation(), instanceRoot());
    ASSERT_TRUE(library);
    const auto error = (*library)->open();
    EXPECT_EQ(error.value_or(""), "USB communication failed");
    EXPECT_EQ((*library)->takeErrorText(), "USB communication failed");
    EXPECT_EQ((*library)->takeErrorText(), ""); // taken once
}

TEST_F(FakeLibrary, StaleInstancesAreCleanedUp) {
    fs::create_directories(instanceRoot() / "instance-old");
    fs::create_directories(instanceRoot() / "instance-new");
    fs::last_write_time(instanceRoot() / "instance-old",
                        fs::file_time_type::clock::now() - std::chrono::hours(25));
    EXPECT_EQ(JLinkLibrary::cleanupStaleInstances(instanceRoot()), 1U);
    EXPECT_FALSE(fs::exists(instanceRoot() / "instance-old"));
    EXPECT_TRUE(fs::exists(instanceRoot() / "instance-new"));
    EXPECT_EQ(JLinkLibrary::cleanupStaleInstances(instanceRoot() / "missing"), 0U);
}

// Review R14: an old instance directory whose lock is held by a live process survives; old ones
// with a stale or missing lock are removed.
TEST_F(FakeLibrary, CleanupKeepsInstancesInUse) {
    auto library = JLinkLibrary::loadCopy(fakeLocation(), instanceRoot());
    ASSERT_TRUE(library);
    const fs::path inUse = (*library)->copyPath().parent_path();
    EXPECT_TRUE(fs::exists(inUse / "instance.lock"));
    fs::last_write_time(inUse, fs::file_time_type::clock::now() - std::chrono::hours(48));
    EXPECT_EQ(JLinkLibrary::cleanupStaleInstances(instanceRoot()), 0U);
    EXPECT_TRUE(fs::exists((*library)->copyPath()));
}

#ifndef _WIN32
TEST_F(FakeLibrary, CleanupRemovesInstancesWithAStaleLock) {
    const fs::path stale = instanceRoot() / "instance-crashed";
    fs::create_directories(stale);
    const QString lockPath = QString::fromStdString((stale / "instance.lock").string());
    QByteArray content;
    {
        QLockFile own(lockPath);
        ASSERT_TRUE(own.tryLock(0));
        QFile file(lockPath);
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        content = file.readAll();
    }
    QProcess gone;
    gone.start(QStringLiteral("sleep"), {QStringLiteral("0.1")});
    ASSERT_TRUE(gone.waitForStarted(5000));
    const qint64 pid = gone.processId();
    ASSERT_TRUE(gone.waitForFinished(5000));
    QList<QByteArray> lines = content.split('\n');
    lines[0] = QByteArray::number(pid);
    lines[1] = "sleep";
    {
        QFile file(lockPath);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(lines.join('\n'));
    }
    fs::last_write_time(stale, fs::file_time_type::clock::now() - std::chrono::hours(48));
    EXPECT_EQ(JLinkLibrary::cleanupStaleInstances(instanceRoot()), 1U);
    EXPECT_FALSE(fs::exists(stale));
}
#endif

TEST_F(FakeLibrary, DefaultInstanceRootIsInAppData) {
    EXPECT_EQ(JLinkLibrary::defaultInstanceRoot().filename(), "jlink-instances");
}

} // namespace
