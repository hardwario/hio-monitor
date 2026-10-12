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

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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

namespace {

// M2 (plan/features/0003, B1): the flashing functions through a real library copy of the fake.
TEST_F(FakeLibrary, FlashFunctionsReachTheLibrary) {
    useScenario(R"({"probes": [{"serialNumber": 5,
        "chip": {"memory": [{"address": 268439680, "hex": "babecafe"}]}}]})");
    auto library = JLinkLibrary::loadCopy(fakeLocation(), instanceRoot());
    ASSERT_TRUE(library) << library.error().detail;
    JLinkLibrary& api = **library;
    ASSERT_EQ(api.selectBySerialNumber(5), 0);
    ASSERT_FALSE(api.open().has_value());
    ASSERT_GE(api.connect(), 0);

    const auto part = api.readU32(0x10000100);
    ASSERT_TRUE(part);
    EXPECT_EQ(*part, 0x52840U);
    EXPECT_FALSE(api.readU32(0x00FF0140)); // the nRF91 FICR does not exist on an nRF52

    std::array<std::byte, 4> pib{};
    ASSERT_EQ(api.readMemory(0x10001080, pib), 4);
    EXPECT_EQ(pib[0], std::byte{0xBA});

    api.beginDownload();
    const std::array<std::byte, 3> data{std::byte{1}, std::byte{2}, std::byte{3}};
    EXPECT_EQ(api.writeMemory(0x2000, data), 3);
    int callbacks = 0;
    EXPECT_GE(api.endDownload([&callbacks](std::string_view action, int /*percent*/) {
        EXPECT_FALSE(action.empty());
        ++callbacks;
    }),
              0);
    EXPECT_GT(callbacks, 0); // the loader's progress reached the FlashProgressFn
    std::array<std::byte, 3> back{};
    ASSERT_EQ(api.readMemory(0x2000, back), 3);
    EXPECT_EQ(back, data);

    EXPECT_GE(api.eraseChip(), 0);
    ASSERT_EQ(api.readMemory(0x2000, back), 3);
    EXPECT_EQ(back[0], std::byte{0xFF});

    ASSERT_GE(api.coresightConfigure(), 0);
    ASSERT_GE(api.coresightWrite(2, false, 1U << 24U), 0);
    const auto status = api.coresightRead(3, true);
    ASSERT_TRUE(status);
    EXPECT_EQ(*status & 1U, 1U); // not protected
}

} // namespace

namespace {

// Review R1: our hook answers NO (DLG_BUTTON_NO). In the fake, a protected chip without a hook is
// mass-erased on connect (what batch mode may do), with the hook the connect fails and nothing is
// erased.
TEST_F(FakeLibrary, UnsecureDialogHookAnswersNo) {
    EXPECT_EQ(JLinkLibrary::unsecureDialogAnswer(), 2);
    const char* scenario = R"({"probes": [{"serialNumber": 5, "chip": {"protected": true,
        "memory": [{"address": 268439680, "hex": "babecafe"}]}}]})";
    {
        useScenario(scenario);
        auto library = JLinkLibrary::loadCopy(fakeLocation(), instanceRoot());
        ASSERT_TRUE(library);
        ASSERT_EQ((*library)->selectBySerialNumber(5), 0);
        ASSERT_FALSE((*library)->open().has_value());
        EXPECT_GE((*library)->connect(), 0); // no hook: unsecured = erased
        std::array<std::byte, 4> pib{};
        ASSERT_EQ((*library)->readMemory(0x10001080, pib), 4);
        EXPECT_EQ(pib[0], std::byte{0xFF});
    }
    useScenario(scenario);
    auto library = JLinkLibrary::loadCopy(fakeLocation(), instanceRoot());
    ASSERT_TRUE(library);
    ASSERT_EQ((*library)->selectBySerialNumber(5), 0);
    ASSERT_FALSE((*library)->open().has_value());
    ASSERT_GE((*library)->installUnsecureDialogHook(), 0);
    EXPECT_LT((*library)->connect(), 0); // NO: still protected, nothing erased
    ASSERT_GE((*library)->coresightConfigure(), 0);
    ASSERT_GE((*library)->coresightWrite(2, false, 1U << 24U), 0);
    EXPECT_EQ((*library)->coresightRead(3, true).value_or(1), 0U); // APPROTECT still enabled
}

} // namespace

namespace {

// ADR-0093: a kept library copy for repeated enumerations (the GUI probe scanner).
TEST_F(FakeLibrary, ProbeEnumeratorKeepsOneCopy) {
    useScenario(R"({"probes": [{"serialNumber": 1, "product": "A"},
                               {"serialNumber": 2, "connection": "ip"}]})");
    const auto oneShot = hio::jlink::enumerateProbes(fakeLocation(), instanceRoot());
    ASSERT_TRUE(oneShot);
    EXPECT_EQ(instanceCount(), 0U);
    {
        auto loaded = hio::jlink::ProbeEnumerator::load(fakeLocation(), instanceRoot());
        ASSERT_TRUE(loaded) << loaded.error().detail;
        hio::jlink::ProbeEnumerator enumerator = std::move(*loaded); // movable
        EXPECT_EQ(instanceCount(), 1U);
        std::vector<fs::path> copies;
        for (int i = 0; i < 5; ++i) {
            const auto probes = enumerator.enumerate();
            ASSERT_TRUE(probes);
            ASSERT_EQ(probes->size(), oneShot->probes.size());
            for (std::size_t j = 0; j < probes->size(); ++j) {
                EXPECT_EQ((*probes)[j].serialNumber, oneShot->probes[j].serialNumber);
                EXPECT_EQ((*probes)[j].connection, oneShot->probes[j].connection);
            }
            EXPECT_EQ(instanceCount(), 1U); // loaded and copied once
        }
        EXPECT_EQ(enumerator.libraryVersion(), oneShot->libraryVersion);
        EXPECT_EQ(enumerator.location().path, fakeLocation().path);
    }
    EXPECT_EQ(instanceCount(), 0U); // the destructor removed the copy
}

} // namespace
