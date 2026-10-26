// JLinkLibrary against the test-only fake library (tests/fake_jlink): real copy + load + symbol
// resolution + version check + out handlers (plan/features/0002, phases A1 and E2).

#include "hio/core/error.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/jlink_library.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/probe_enumerator.h"
#include "hio/hw/jlink/types.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QLatin1Char>
#include <QList>
#include <QLockFile>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTemporaryDir>
#include <QtEnvironmentVariables>
#include <QtLogging>
#include <QtTypes>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
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
    // HIL H3: the fake's select fails while a flash progress callback is registered, as the real
    // library did; the loader must not register it.
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
    EXPECT_GT(callbacks, 0);                   // the loader's progress reached the FlashProgressFn
    EXPECT_EQ(api.selectBySerialNumber(5), 0); // HIL H3: the callback is cleared afterwards
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

// HIL H3: the progress callback is also cleared when EndDownload fails.
TEST_F(FakeLibrary, FlashProgressCallbackIsClearedAfterAFailedDownload) {
    useScenario(R"({"probes": [{"serialNumber": 5, "chip": {"writeFault": true}}]})");
    auto library = JLinkLibrary::loadCopy(fakeLocation(), instanceRoot());
    ASSERT_TRUE(library) << library.error().detail;
    JLinkLibrary& api = **library;
    ASSERT_EQ(api.selectBySerialNumber(5), 0);
    ASSERT_FALSE(api.open().has_value());
    ASSERT_GE(api.connect(), 0);
    api.beginDownload();
    const std::array<std::byte, 1> data{std::byte{1}};
    EXPECT_EQ(api.writeMemory(0x2000, data), 1);
    EXPECT_LT(api.endDownload([](std::string_view /*action*/, int /*percent*/) {}), 0);
    EXPECT_EQ(api.selectBySerialNumber(5), 0);
}

// Phase 0 item 0.10, ADR-0109: closing the J-Link during a download programs the buffered bytes
// (the fake models what J-Link 9.84 did on hardware).
TEST_F(FakeLibrary, CloseDuringADownloadProgramsTheBuffer) {
    useScenario(R"({"probes": [{"serialNumber": 5}]})");
    auto library = JLinkLibrary::loadCopy(fakeLocation(), instanceRoot());
    ASSERT_TRUE(library) << library.error().detail;
    JLinkLibrary& api = **library;
    ASSERT_EQ(api.selectBySerialNumber(5), 0);
    ASSERT_FALSE(api.open().has_value());
    ASSERT_GE(api.connect(), 0);
    api.beginDownload();
    const std::array<std::byte, 2> data{std::byte{0xA5}, std::byte{0xA5}};
    EXPECT_EQ(api.writeMemory(0x3000, data), 2);
    api.close();
    ASSERT_FALSE(api.open().has_value());
    ASSERT_GE(api.connect(), 0);
    std::array<std::byte, 2> back{};
    ASSERT_EQ(api.readMemory(0x3000, back), 2);
    EXPECT_EQ(back, data);
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

namespace {

using hio::jlink::OptionalFunction;

constexpr std::array allOptionalFunctions{
    OptionalFunction::IsHalted,        OptionalFunction::GetId,
    OptionalFunction::WriteU16,        OptionalFunction::WriteU32,
    OptionalFunction::GetRegisterList, OptionalFunction::GetRegisterName,
    OptionalFunction::ReadRegs,        OptionalFunction::GetMemZones};
static_assert(allOptionalFunctions.size() == hio::jlink::optionalFunctionCount);

// M6 (plan/features/0013, ADR-0151): the optional target functions through a real copy of the
// fake: halted state, RAM, registers, zones.
TEST_F(FakeLibrary, TargetFunctionsReachTheLibrary) {
    useScenario(R"json({"probes": [{"serialNumber": 5, "chip": {
        "ram": [{"address": 536870912, "size": 256}],
        "registers": [{"name": "R0", "value": 17}, {"name": "PC", "value": 4096}],
        "zones": [{"name": "AHB-AP", "description": "AHB-AP (AP0)", "virtualAddress": 0}],
        "coreId": 305419896}}]})json");
    auto library = JLinkLibrary::loadCopy(fakeLocation(), instanceRoot());
    ASSERT_TRUE(library) << library.error().detail;
    JLinkLibrary& api = **library;
    for (const OptionalFunction function : allOptionalFunctions) {
        EXPECT_TRUE(api.has(function)) << hio::jlink::optionalFunctionName(function);
    }
    ASSERT_EQ(api.selectBySerialNumber(5), 0);
    ASSERT_FALSE(api.open().has_value());
    ASSERT_GE(api.connect(), 0);

    EXPECT_EQ(api.isHalted(), 0);
    EXPECT_EQ(api.halt(), 0);
    EXPECT_GT(api.isHalted(), 0);
    api.go();
    EXPECT_EQ(api.isHalted(), 0);
    ASSERT_GE(api.reset(), 0);
    EXPECT_GT(api.isHalted(), 0); // the IJLinkApi contract: halted after a reset
    api.go();
    EXPECT_EQ(api.coreId(), 0x12345678U);

    // RAM: WriteMem outside a download, WriteU16 / WriteU32, reads with every width.
    const std::array<std::byte, 2> bytes{std::byte{0x11}, std::byte{0x22}};
    EXPECT_EQ(api.writeMemory(0x20000000, bytes), 2);
    EXPECT_EQ(api.writeU16(0x20000002, 0x4433), 0);
    EXPECT_EQ(api.writeU32(0x20000004, 0x88776655U), 0);
    EXPECT_LT(api.writeU32(0x2000, 1), 0);            // flash is not RAM
    EXPECT_LT(api.writeMemory(0x30000000, bytes), 0); // unmapped
    std::array<std::byte, 8> back{};
    ASSERT_EQ(api.readMemory(0x20000000, back), 8);
    EXPECT_EQ(back[0], std::byte{0x11});
    EXPECT_EQ(back[3], std::byte{0x44});
    EXPECT_EQ(back[7], std::byte{0x88});
    std::array<std::byte, 4> halfwords{};
    EXPECT_EQ(api.readMemory16(0x20000002, halfwords), 4);
    EXPECT_EQ(halfwords[0], std::byte{0x33});
    std::array<std::uint32_t, 2> words{};
    EXPECT_EQ(api.readU32s(0x20000000, words), 2);
    EXPECT_EQ(words[0], 0x44332211U);
    EXPECT_EQ(words[1], 0x88776655U);

    const auto list = api.registerList();
    ASSERT_TRUE(list);
    ASSERT_EQ(list->size(), 2U);
    EXPECT_EQ(api.registerName((*list)[1]), "PC");
    std::array<std::uint32_t, 2> values{};
    EXPECT_GE(api.readRegisters(*list, values), 0);
    EXPECT_EQ(values[0], 17U);
    EXPECT_EQ(values[1], 4096U);

    const auto zones = api.memoryZones(32);
    ASSERT_TRUE(zones);
    ASSERT_EQ(zones->size(), 1U);
    EXPECT_EQ((*zones)[0].name, "AHB-AP");
    EXPECT_EQ((*zones)[0].description, "AHB-AP (AP0)");
    EXPECT_EQ(api.memoryZones(0).value_or(std::vector<hio::jlink::MemoryZone>{{}}).size(), 0U);
}

// The hio.jlink messages logged while it lives (owner thread only; restores the previous handler).
class JLinkLogCapture {
public:
    JLinkLogCapture() : m_previous(qInstallMessageHandler(&JLinkLogCapture::handle)) {}
    ~JLinkLogCapture() { qInstallMessageHandler(m_previous); }
    JLinkLogCapture(const JLinkLogCapture&) = delete;
    JLinkLogCapture& operator=(const JLinkLogCapture&) = delete;
    JLinkLogCapture(JLinkLogCapture&&) = delete;
    JLinkLogCapture& operator=(JLinkLogCapture&&) = delete;

    [[nodiscard]] static QStringList& messages() {
        static QStringList captured;
        return captured;
    }

private:
    static void handle(QtMsgType /*type*/, const QMessageLogContext& context,
                       const QString& message) {
        if (context.category != nullptr && std::string_view(context.category) == "hio.jlink") {
            messages().append(message);
        }
    }

    QtMessageHandler m_previous;
};

// Review S10: a memory zone list longer than the limit is cut, and the cut is logged.
TEST_F(FakeLibrary, MemoryZoneTruncationIsLogged) {
    QString zones;
    for (int i = 0; i < 33; ++i) {
        zones +=
            QStringLiteral(R"({"name": "Z%1", "description": "", "virtualAddress": %1},)").arg(i);
    }
    zones.chop(1);
    const QByteArray scenario =
        QStringLiteral(R"({"probes": [{"serialNumber": 5, "chip": {"zones": [%1]}}]})")
            .arg(zones)
            .toUtf8();
    useScenario(scenario.constData());
    auto library = JLinkLibrary::loadCopy(fakeLocation(), instanceRoot());
    ASSERT_TRUE(library) << library.error().detail;
    JLinkLibrary& api = **library;
    ASSERT_EQ(api.selectBySerialNumber(5), 0);
    ASSERT_FALSE(api.open().has_value());
    ASSERT_GE(api.connect(), 0);
    JLinkLogCapture::messages().clear();
    {
        const JLinkLogCapture capture;
        const auto cut = api.memoryZones(32);
        ASSERT_TRUE(cut);
        EXPECT_EQ(cut->size(), 32U);
        EXPECT_EQ(cut->back().name, "Z31");
    }
    EXPECT_TRUE(JLinkLogCapture::messages().contains(
        QStringLiteral("Memory zone list truncated from 33 to 32")))
        << JLinkLogCapture::messages().join(QLatin1Char('|')).toStdString();
}

// ADR-0151: a library without the optional exports still loads; has() reports them missing and
// the calls fail without touching the library.
TEST_F(FakeLibrary, MissingOptionalFunctionsDoNotFailTheLoad) {
    useScenario(R"({"probes": [{"serialNumber": 5}]})");
    auto library = JLinkLibrary::loadCopy(
        LibraryLocation{.path = fs::path(HIO_FAKE_JLINK_BASIC_LIBRARY)}, instanceRoot());
    ASSERT_TRUE(library) << library.error().detail;
    JLinkLibrary& api = **library;
    for (const OptionalFunction function : allOptionalFunctions) {
        EXPECT_FALSE(api.has(function)) << hio::jlink::optionalFunctionName(function);
    }
    ASSERT_EQ(api.selectBySerialNumber(5), 0);
    ASSERT_FALSE(api.open().has_value());
    ASSERT_GE(api.connect(), 0);
    EXPECT_LT(api.isHalted(), 0);
    EXPECT_EQ(api.coreId(), 0U);
    EXPECT_LT(api.writeU32(0x20000000, 1), 0);
    EXPECT_FALSE(api.registerList());
    EXPECT_FALSE(api.memoryZones(32));
    EXPECT_EQ(hio::jlink::optionalFunctionName(OptionalFunction::GetMemZones), "JLINK_GetMemZones");
}

// M6: RTT START carries the control block address (JLINK_RTTERMINAL_START); with "autoSearch"
// false the fake finds the block only at the scenario's address.
TEST_F(FakeLibrary, RttStartPassesTheBlockAddress) {
    useScenario(R"({"probes": [{"serialNumber": 5,
        "rtt": {"blockAddress": 536875008, "autoSearch": false}}]})");
    auto library = JLinkLibrary::loadCopy(fakeLocation(), instanceRoot());
    ASSERT_TRUE(library) << library.error().detail;
    JLinkLibrary& api = **library;
    ASSERT_EQ(api.selectBySerialNumber(5), 0);
    ASSERT_FALSE(api.open().has_value());
    ASSERT_GE(api.connect(), 0);
    ASSERT_GE(api.rttStart(std::nullopt), 0);
    EXPECT_LT(api.rttBufferCount(hio::jlink::RttDirection::Up), 0); // no search
    ASSERT_GE(api.rttStart(0x20000000U), 0);
    EXPECT_LT(api.rttBufferCount(hio::jlink::RttDirection::Up), 0); // wrong address
    ASSERT_GE(api.rttStart(0x20001000U), 0);
    EXPECT_EQ(api.rttBufferCount(hio::jlink::RttDirection::Up), 2);
}

} // namespace
