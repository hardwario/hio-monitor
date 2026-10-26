// The device tools on real sessions (plan/features/0013 "Tests: Tools"): DeviceSessions on the
// fake J-Link library and BleSessions on the fake BLE backend (HIO_TEST_OVERRIDES builds).
// connect / disconnect, send_command by prompt / silence / timeout with the fake's responses and
// echo, the session control tools, every target tool, flash (path, catalog id, both, UICR with
// and without allow_uicr, cancel, stall), erase, recover, internal.busy with the kind, the device
// closed mid-call, a BLE session (mcp.capability_missing), a missing optional J-Link export.

#include "hex_builder.h"
#include "hio/device/console_line.h"
#include "hio/firmware/firmware_cache.h"
#include "hio/firmware/firmware_catalog.h"
#include "hio/firmware/firmware_image.h"
#include "hio/mcp/consent.h"
#include "hio/mcp/limits.h"
#include "hio/mcp/tools/device_provider.h"
#include "hio/mcp/tools/tools_context.h"
#include "http_fixture.h"
#include "session_provider.h"
#include "tools_test_support.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringLiteral>
#include <QUrl>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hio::mcp::tools::test {
namespace {

using namespace std::chrono_literals;
namespace fs = std::filesystem;

constexpr const char* device = "jlink:801000001";

// py-hardwario 1.7.2 PIB vector (libs/device/tests/pib_test.cpp).
constexpr const char* pibHex =
    "babecafe027b48415244574152494f0000000000000000434845535445522d4d00000000000000004344474c53"
    "00000000000052332e3200000032313539303138323437003031323334353637383961626364656630313233343536"
    "373839616263646566003132333435360000000000000000000000a8d83379ffffffffff";

QJsonObject scenario(const QJsonObject& rttExtra = {}, const QJsonObject& chipExtra = {}) {
    QJsonObject rtt{
        {QStringLiteral("output"),
         QJsonArray{QJsonObject{{QStringLiteral("atMs"), 0},
                                {QStringLiteral("buffer"), 0},
                                {QStringLiteral("text"), QStringLiteral("booted\n")}}}},
        {QStringLiteral("responses"),
         QJsonObject{{QStringLiteral("version"), QStringLiteral("Firmware version: v3\n")}}}};
    for (auto it = rttExtra.begin(); it != rttExtra.end(); ++it) {
        rtt.insert(it.key(), it.value());
    }
    QJsonObject chip{
        {QStringLiteral("ram"), QJsonArray{QJsonObject{{QStringLiteral("address"), 0x20000000},
                                                       {QStringLiteral("size"), 4096}}}},
        {QStringLiteral("registers"),
         QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("R0")},
                                {QStringLiteral("value"), 1}},
                    QJsonObject{{QStringLiteral("name"), QStringLiteral("PC")},
                                {QStringLiteral("value"), 0x1000}}}},
        {QStringLiteral("zones"),
         QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("AHB-AP")},
                                {QStringLiteral("description"), QStringLiteral("AHB memory")},
                                {QStringLiteral("virtualAddress"), 0}}}},
        {QStringLiteral("coreId"), 0x2BA01477},
        {QStringLiteral("memory"),
         QJsonArray{QJsonObject{{QStringLiteral("address"), 0x10001080},
                                {QStringLiteral("hex"), QString::fromLatin1(pibHex)}}}}};
    for (auto it = chipExtra.begin(); it != chipExtra.end(); ++it) {
        chip.insert(it.key(), it.value());
    }
    return QJsonObject{
        {QStringLiteral("probes"),
         QJsonArray{QJsonObject{{QStringLiteral("serialNumber"), 801000001},
                                {QStringLiteral("product"), QStringLiteral("Test Probe")},
                                {QStringLiteral("rtt"), rtt},
                                {QStringLiteral("chip"), chip}}}}};
}

std::string hexFile(const fs::path& path, std::vector<hio::firmware::Segment> segments) {
    auto image = hio::firmware::FirmwareImage::fromSegments(std::move(segments));
    const std::string text = hio::firmware::test::toHex(*image);
    QFile file(QString::fromStdString(path.string()));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(text.data(), static_cast<qint64>(text.size()));
    }
    return path.string();
}

class JLinkTools : public ::testing::Test {
protected:
    void SetUp() override { provider().scenario(QJsonDocument(scenario()).toJson()); }

    static hio::mcp::Limits limits() {
        hio::mcp::Limits shrunk;
        shrunk.tools.sendCommandSilence = 150ms;
        return shrunk;
    }

    // connect + wait until the console is connected.
    static void connectDevice(Harness& harness) {
        auto call = harness.call(QStringLiteral("connect"), args({{"device", device}}));
        ASSERT_FALSE(call.isError()) << call.errorCode() << " " << call.errorDetail();
        auto connected =
            harness.call(QStringLiteral("wait_for_connection"), args({{"timeout", 10}}));
        ASSERT_TRUE(connected.structured().value(QStringLiteral("connected")).toBool());
        harness.consent.requests.clear(); // the connect request
    }

    [[nodiscard]] std::string imagePath(std::uint32_t address, std::size_t size,
                                        const char* name = "app.hex") {
        return hexFile(provider().root() / name,
                       {hio::firmware::Segment{.address = address,
                                               .data = hio::firmware::test::filled(size, 0x5A)}});
    }

    [[nodiscard]] SessionProvider& provider() { return m_provider; }

private:
    SessionProvider m_provider;
};

TEST_F(JLinkTools, ConnectStatusAndDisconnect) {
    provider().settings().available.push_back(DeviceEntry{.id = "jlink:801000009",
                                                          .kind = "jlink",
                                                          .displayName = "J-Link 801000009",
                                                          .profileId = {},
                                                          .state = DeviceEntry::State::Available,
                                                          .owner = std::nullopt,
                                                          .connection = std::nullopt,
                                                          .openedByMcp = false});
    Harness harness(provider(), limits());
    auto connect = harness.call(QStringLiteral("connect"), args({{"device", device}}));
    ASSERT_FALSE(connect.isError()) << connect.errorCode() << " " << connect.errorDetail();
    EXPECT_FALSE(connect.structured().value(QStringLiteral("already_open")).toBool());
    EXPECT_EQ(connect.structured()
                  .value(QStringLiteral("connection"))
                  .toObject()
                  .value(QStringLiteral("state"))
                  .toString(),
              QStringLiteral("connected"));
    auto again = harness.call(QStringLiteral("connect"), args({{"device", device}}));
    EXPECT_TRUE(again.structured().value(QStringLiteral("already_open")).toBool());

    auto list = harness.call(QStringLiteral("list_devices"), args({{"refresh", true}}));
    EXPECT_EQ(provider().refreshes(), 1);
    EXPECT_EQ(list.structured().value(QStringLiteral("devices")).toArray().size(), 2);

    auto disconnect = harness.call(QStringLiteral("disconnect"));
    EXPECT_TRUE(disconnect.structured().value(QStringLiteral("closed")).toBool());
    EXPECT_EQ(harness.call(QStringLiteral("status"))
                  .structured()
                  .value(QStringLiteral("devices"))
                  .toArray()
                  .size(),
              0);
}

TEST_F(JLinkTools, ConnectFailureClosesTheSession) {
    QJsonObject failing = scenario();
    QJsonArray probes = failing.value(QStringLiteral("probes")).toArray();
    QJsonObject probe = probes[0].toObject();
    probe.insert(QStringLiteral("openError"), QStringLiteral("USB error"));
    probes[0] = probe;
    failing.insert(QStringLiteral("probes"), probes);
    provider().scenario(QJsonDocument(failing).toJson());
    Harness harness(provider());
    auto call = harness.call(QStringLiteral("connect"), args({{"device", device}}));
    EXPECT_TRUE(call.isError());
    EXPECT_TRUE(runUntil([&] { return provider().list().empty(); }));
}

TEST_F(JLinkTools, SendCommandEndsAtTheRttPromptAndDropsTheShellEcho) {
    provider().settings().profile = &promptProfile();
    provider().scenario(
        QJsonDocument(scenario(QJsonObject{
                          {QStringLiteral("echo"), true},
                          {QStringLiteral("responses"),
                           QJsonObject{{QStringLiteral("version"),
                                        QStringLiteral("Firmware version: v3\nuart:~$ ")}}}}))
            .toJson());
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto call = harness.call(QStringLiteral("send_command"), args({{"command", "version"}}));
    ASSERT_FALSE(call.isError()) << call.errorCode();
    EXPECT_EQ(call.structured().value(QStringLiteral("end")).toString(), QStringLiteral("prompt"));
    const QJsonArray output = call.structured().value(QStringLiteral("output")).toArray();
    ASSERT_EQ(output.size(), 1);
    EXPECT_EQ(output[0].toObject().value(QStringLiteral("text")).toString(),
              QStringLiteral("Firmware version: v3"));
}

TEST_F(JLinkTools, SendCommandEndsBySilenceAndTimeout) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto silence = harness.call(QStringLiteral("send_command"), args({{"command", "version"}}));
    EXPECT_EQ(silence.structured().value(QStringLiteral("end")).toString(),
              QStringLiteral("silence"));
    EXPECT_EQ(silence.structured().value(QStringLiteral("output")).toArray().size(), 1);
    auto timeout = harness.call(QStringLiteral("send_command"),
                                args({{"command", "unknown"}, {"timeout", 0.5}}));
    EXPECT_EQ(timeout.structured().value(QStringLiteral("end")).toString(),
              QStringLiteral("timeout"));
}

TEST_F(JLinkTools, SessionControlTools) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto stop = harness.call(QStringLiteral("stop"));
    ASSERT_FALSE(stop.isError()) << stop.errorCode();
    EXPECT_TRUE(stop.structured().value(QStringLiteral("was_reading")).toBool());
    ASSERT_TRUE(runUntil([&] {
        return harness.call(QStringLiteral("wait_for_connection"), args({{"timeout", 1}}))
                   .structured()
                   .value(QStringLiteral("state"))
                   .toString() == QStringLiteral("stopped");
    }));
    auto start = harness.call(QStringLiteral("start"));
    EXPECT_FALSE(start.structured().value(QStringLiteral("already_reading")).toBool());
    EXPECT_TRUE(harness.call(QStringLiteral("wait_for_connection"))
                    .structured()
                    .value(QStringLiteral("connected"))
                    .toBool());
    auto reconnect = harness.call(QStringLiteral("reconnect"), args({{"rtt_address", "auto"}}));
    EXPECT_FALSE(reconnect.isError()) << reconnect.errorCode();
    auto bad = harness.call(QStringLiteral("reconnect"), args({{"rtt_address", "zzz"}}));
    EXPECT_EQ(bad.rpcError(), -32602);
    auto release = harness.call(QStringLiteral("release_probe"));
    EXPECT_TRUE(release.structured().value(QStringLiteral("released")).toBool());
    auto reacquire = harness.call(QStringLiteral("reacquire_probe"));
    EXPECT_FALSE(reacquire.isError()) << reacquire.errorCode();
    EXPECT_TRUE(harness.call(QStringLiteral("wait_for_connection"), args({{"timeout", 10}}))
                    .structured()
                    .value(QStringLiteral("connected"))
                    .toBool());
    // A marker for every control tool (ADR-0105), none for the reads.
    EXPECT_EQ(provider().record().markers.size(), 5U);
}

TEST_F(JLinkTools, HaltGoStatusRegistersZones) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto status = harness.call(QStringLiteral("target_status"));
    ASSERT_FALSE(status.isError()) << status.errorCode() << " " << status.errorDetail();
    EXPECT_FALSE(status.structured().value(QStringLiteral("halted")).toBool());
    EXPECT_EQ(status.structured().value(QStringLiteral("core_id")).toString(),
              QStringLiteral("0x2BA01477"));
    auto notHalted = harness.call(QStringLiteral("read_registers"));
    EXPECT_EQ(notHalted.errorCode(), "target.not_halted");
    auto halt = harness.call(QStringLiteral("halt"));
    EXPECT_TRUE(halt.structured().value(QStringLiteral("halted")).toBool());
    auto registers = harness.call(QStringLiteral("read_registers"));
    ASSERT_FALSE(registers.isError()) << registers.errorCode();
    EXPECT_EQ(registers.structured().value(QStringLiteral("registers")).toObject(),
              (QJsonObject{{QStringLiteral("R0"), QStringLiteral("0x00000001")},
                           {QStringLiteral("PC"), QStringLiteral("0x00001000")}}));
    auto go = harness.call(QStringLiteral("go"));
    EXPECT_FALSE(go.structured().value(QStringLiteral("halted")).toBool());
    auto zones = harness.call(QStringLiteral("memory_zones"));
    const QJsonArray list = zones.structured().value(QStringLiteral("zones")).toArray();
    ASSERT_EQ(list.size(), 1);
    EXPECT_EQ(list[0].toObject().value(QStringLiteral("virtual_address")).toString(),
              QStringLiteral("0x00000000"));
    auto badTarget = harness.call(QStringLiteral("halt"), args({{"target", "modem"}}));
    EXPECT_EQ(badTarget.errorCode(), "usage.invalid_argument");
    // halt and go wrote markers; the console stays connected (ADR-0157).
    EXPECT_EQ(provider().record().markers.size(), 2U);
}

TEST_F(JLinkTools, WriteAndReadMemory) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto write = harness.call(QStringLiteral("write_memory"),
                              args({{"address", "0x20000010"}, {"data_hex", "deadbeef"}}));
    ASSERT_FALSE(write.isError()) << write.errorCode() << " " << write.errorDetail();
    EXPECT_EQ(write.structured().value(QStringLiteral("units_written")).toInteger(), 4);
    EXPECT_EQ(write.structured().value(QStringLiteral("address")).toString(),
              QStringLiteral("0x20000010"));
    ASSERT_EQ(harness.consent.requests.size(), 1U);
    EXPECT_EQ(harness.consent.requests[0].categories,
              hio::mcp::ConsentCategories(hio::mcp::ConsentCategory::MemoryWrite));
    EXPECT_EQ(harness.consent.requests[0].details.value(QStringLiteral("range")).toString(),
              QStringLiteral("0x20000010-0x20000013"));

    auto read = harness.call(QStringLiteral("read_memory"),
                             args({{"address", "0x20000010"}, {"length", 8}, {"width", 32}}));
    ASSERT_FALSE(read.isError()) << read.errorCode() << " " << read.errorDetail();
    EXPECT_EQ(read.structured().value(QStringLiteral("hex")).toString(),
              QStringLiteral("deadbeef00000000"));
    EXPECT_EQ(
        read.structured().value(QStringLiteral("dump")).toArray().at(0).toString(),
        QStringLiteral("0x20000010: de ad be ef 00 00 00 00                         |........|"));

    // Review F4: the range is checked before the consent request (no request is recorded);
    // ADR-0164: the System Control Space (VTOR, STIR) and the FPB are denied too.
    const std::size_t requestsBefore = harness.consent.requests.size();
    for (const char* address : {"0x00001000", "0xE000ED08", "0xE000EF00", "0xE0002000"}) {
        auto outOfRange = harness.call(QStringLiteral("write_memory"),
                                       args({{"address", address}, {"data_hex", "00000000"}}));
        EXPECT_EQ(outOfRange.errorCode(), "target.address_out_of_range") << address;
        EXPECT_TRUE(outOfRange.isError()) << address;
    }
    EXPECT_EQ(harness.consent.requests.size(), requestsBefore);
    auto both = harness.call(QStringLiteral("write_memory"),
                             args({{"address", "0x20000000"}, {"data_hex", "00"}, {"path", "/x"}}));
    EXPECT_EQ(both.rpcError(), -32602);
    auto neither = harness.call(QStringLiteral("write_memory"), args({{"address", "0x20000000"}}));
    EXPECT_EQ(neither.rpcError(), -32602);
    auto badHex = harness.call(QStringLiteral("write_memory"),
                               args({{"address", "0x20000000"}, {"data_hex", "abc"}}));
    EXPECT_EQ(badHex.rpcError(), -32602);
    auto badWidth = harness.call(QStringLiteral("read_memory"),
                                 args({{"address", "0x20000000"}, {"width", 7}}));
    EXPECT_EQ(badWidth.rpcError(), -32602);
    auto badAddress = harness.call(QStringLiteral("read_memory"), args({{"address", "0xZZ"}}));
    EXPECT_EQ(badAddress.rpcError(), -32602);
    auto tooLong =
        harness.call(QStringLiteral("read_memory"), args({{"address", "0x0"}, {"length", 65537}}));
    EXPECT_EQ(tooLong.rpcError(), -32602);

    harness.consent.mode = hio::mcp::test::FakeConsentBroker::Mode::Deny;
    auto denied = harness.call(QStringLiteral("write_memory"),
                               args({{"address", "0x20000000"}, {"data_hex", "00"}}));
    EXPECT_EQ(denied.errorCode(), "mcp.tool_not_allowed");
}

TEST_F(JLinkTools, ReadMemoryToFile) {
    ToolsOptions options;
    options.memoryDumpDirectory = provider().root() / "dumps";
    Harness harness(provider(), limits(), nullptr, options);
    connectDevice(harness);
    auto dump =
        harness.call(QStringLiteral("read_memory"),
                     args({{"address", "0x0"}, {"length", 70000}, {"to_file", "flash.bin"}}));
    ASSERT_FALSE(dump.isError()) << dump.errorCode() << " " << dump.errorDetail();
    const QJsonObject result = dump.structured();
    const QString file = result.value(QStringLiteral("file")).toString();
    EXPECT_EQ(fs::path(file.toStdString()), provider().root() / "dumps" / "flash.bin");
    EXPECT_EQ(result.value(QStringLiteral("length")).toInteger(), 70000);
    QFile bytes(file);
    ASSERT_TRUE(bytes.open(QIODevice::ReadOnly));
    const QByteArray content = bytes.readAll();
    EXPECT_EQ(content.size(), 70000);
    EXPECT_EQ(
        result.value(QStringLiteral("sha256")).toString(),
        QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex()));
    EXPECT_EQ(result.value(QStringLiteral("preview")).toArray().size(), 4);

    auto exists = harness.call(QStringLiteral("read_memory"),
                               args({{"address", "0x0"}, {"length", 16}, {"to_file", file}}));
    EXPECT_EQ(exists.errorCode(), "io.file_exists");
    EXPECT_TRUE(QFile::exists(file)); // never overwritten, never removed
    auto relative = harness.call(QStringLiteral("read_memory"),
                                 args({{"address", "0x0"}, {"to_file", "sub/x.bin"}}));
    EXPECT_EQ(relative.rpcError(), -32602);
}

// Review R15: no folder is created for an absolute path; bare names have no ':' on any OS; the
// server's own cache folder is created for a bare name.
TEST_F(JLinkTools, ReadMemoryToFilePathRules) {
    ToolsOptions options;
    options.memoryDumpDirectory = provider().root() / "cache" / "mcp"; // does not exist yet
    Harness harness(provider(), limits(), nullptr, options);
    connectDevice(harness);
    const fs::path missing = provider().root() / "missing" / "dump.bin";
    auto noFolder = harness.call(QStringLiteral("read_memory"),
                                 args({{"address", "0x0"},
                                       {"length", 16},
                                       {"to_file", QString::fromStdString(missing.string())}}));
    EXPECT_EQ(noFolder.errorCode(), "io.write_failed");
    EXPECT_NE(noFolder.errorDetail().find(missing.string()), std::string::npos)
        << noFolder.errorDetail();
    EXPECT_FALSE(fs::exists(missing.parent_path()));
    // Review F5: Windows device names (any case, with an extension) and trailing dots / spaces.
    for (const char* name : {"C:foo.bin", "dump.bin:stream", "C:", "..", "a\\b.bin", "NUL", "nul",
                             "nul.txt", "Con", "prn.bin", "AUX", "COM1.bin", "com9", "LPT1",
                             "lpt5.dump", "nul .txt", "dump.", "dump.bin ", "dump. "}) {
        auto refused = harness.call(QStringLiteral("read_memory"),
                                    args({{"address", "0x0"}, {"to_file", name}}));
        EXPECT_EQ(refused.rpcError(), -32602) << name;
        EXPECT_EQ(refused.rpcPointer(), QStringLiteral("/arguments/to_file")) << name;
    }
    // Not device names: no digit / digit 0 / longer stems.
    for (const char* name :
         {"COM.bin", "com0.bin", "LPT10.bin", "console.bin", "nul_dump.bin", "auxiliary"}) {
        auto accepted = harness.call(QStringLiteral("read_memory"),
                                     args({{"address", "0x0"}, {"length", 16}, {"to_file", name}}));
        EXPECT_FALSE(accepted.isError()) << name << " " << accepted.errorCode();
    }
    auto bare = harness.call(QStringLiteral("read_memory"),
                             args({{"address", "0x0"}, {"length", 16}, {"to_file", "d.bin"}}));
    ASSERT_FALSE(bare.isError()) << bare.errorCode() << " " << bare.errorDetail();
    EXPECT_TRUE(fs::exists(provider().root() / "cache" / "mcp" / "d.bin"));
}

// Review R13: a path with "%1" appears unchanged in the message (no chained arg()).
TEST_F(JLinkTools, PathWithPercentPlaceholderInMessages) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    const fs::path big = provider().root() / "big%1%2.bin";
    {
        QFile file(QString::fromStdString(big.string()));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QByteArray((64 * 1024) + 1, 'x'));
    }
    auto tooBig = harness.call(
        QStringLiteral("write_memory"),
        args({{"address", "0x20000000"}, {"path", QString::fromStdString(big.string())}}));
    EXPECT_EQ(tooBig.errorCode(), "usage.invalid_argument");
    EXPECT_EQ(tooBig.error().value(QStringLiteral("message")).toString(),
              QStringLiteral("The file %1 is larger than 65536 bytes.")
                  .arg(QString::fromStdString(big.string())));
}

// ADR-0162 / review R11 (TSan): the Intel HEX parse of `flash` and the classification of
// `flash_lte` run on the server's offloader; the call goes on on the owner thread afterwards.
TEST_F(JLinkTools, FlashParseAndLteClassificationAreOffloaded) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    const QString path = QString::fromStdString(imagePath(0x0, 8192));
    auto flash = harness.start(QStringLiteral("flash"), args({{"path", path}}));
    // Submitted from the call's constructor; its result is only delivered by the event loop.
    EXPECT_EQ(harness.server->offloader().running(), 1U);
    EXPECT_TRUE(harness.consent.requests.empty()); // consent follows the parse
    ASSERT_TRUE(runUntil([&] { return flash.done(); }, 30s));
    ASSERT_FALSE(flash.isError()) << flash.errorCode() << " " << flash.errorDetail();
    EXPECT_EQ(harness.server->offloader().running(), 0U);

    auto lte = harness.start(QStringLiteral("flash_lte"), args({{"path", path}}));
    EXPECT_EQ(harness.server->offloader().running(), 1U);
    ASSERT_TRUE(runUntil([&] { return lte.done(); }, 30s));
    // The fake chip is an nRF52, so the LTE target cannot be programmed - but the input was
    // classified (an application .hex) and the operation was asked for after consent.
    ASSERT_FALSE(harness.consent.requests.empty());
    EXPECT_EQ(harness.consent.requests.back().details.value(QStringLiteral("kind")).toString(),
              QStringLiteral("hex"));

    // A parse error comes back as the tool error.
    const fs::path broken = provider().root() / "broken.hex";
    {
        QFile file(QString::fromStdString(broken.string()));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(":zz\n");
    }
    auto bad = harness.call(QStringLiteral("flash"),
                            args({{"path", QString::fromStdString(broken.string())}}), 30s);
    EXPECT_EQ(bad.errorCode(), "firmware.hex_parse_error");
}

// ADR-0162: a full offloader (running and queued) answers mcp.too_many_requests; a cancelled
// call drops its job.
TEST_F(JLinkTools, OffloaderFullIsTooManyRequests) {
    hio::mcp::Limits shrunk = limits();
    shrunk.maxOffloadRunning = 1;
    shrunk.maxOffloadQueued = 0;
    Harness harness(provider(), shrunk);
    connectDevice(harness);
    const QString path = QString::fromStdString(imagePath(0x0, 4096));
    auto first = harness.start(QStringLiteral("flash"), args({{"path", path}}));
    const int firstId = harness.lastId();
    auto second = harness.call(QStringLiteral("flash"), args({{"path", path}}));
    EXPECT_EQ(second.errorCode(), "mcp.too_many_requests");
    harness.cancel(firstId); // cancelled while parsing: no consent, no flash
    runFor(300ms);
    EXPECT_TRUE(harness.consent.requests.empty());
    EXPECT_TRUE(provider().record().busy.empty());
    EXPECT_EQ(harness.server->offloader().running(), 0U);
    (void)first;
}

// Review R14: a cancel that arrives after the operation already succeeded reports the success.
TEST_F(JLinkTools, CancelRacingCompletionReportsTheSuccess) {
    provider().scenario(
        QJsonDocument(scenario({}, QJsonObject{{QStringLiteral("readDelayMs"), 600}})).toJson());
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto read = harness.start(QStringLiteral("read_memory"),
                              args({{"address", "0x20000000"}, {"length", 4}}));
    const int id = harness.lastId();
    runFor(200ms); // the read runs on the worker (one ReadMemEx, not interruptible)
    harness.cancel(id);
    ASSERT_TRUE(runUntil(
        [&] {
            return !harness.activity.entries.empty() &&
                   harness.activity.entries.back().tool == "read_memory";
        },
        10s));
    EXPECT_EQ(harness.activity.entries.back().result, "ok");
    (void)read;
}

TEST_F(JLinkTools, PibAndUicr) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto pib = harness.call(QStringLiteral("read_pib"));
    ASSERT_FALSE(pib.isError()) << pib.errorCode() << " " << pib.errorDetail();
    EXPECT_EQ(pib.structured().value(QStringLiteral("signature")).toString(),
              QStringLiteral("0xbabecafe"));
    EXPECT_EQ(pib.structured().value(QStringLiteral("product_name")).toString(),
              QStringLiteral("CHESTER-M"));
    auto uicr = harness.call(QStringLiteral("read_uicr"));
    EXPECT_EQ(uicr.structured().value(QStringLiteral("bytes")).toInteger(), 128);
    EXPECT_TRUE(uicr.structured()
                    .value(QStringLiteral("hex"))
                    .toString()
                    .startsWith(QStringLiteral("babecafe")));
}

TEST_F(JLinkTools, ResetWritesMarkersAndCursors) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto reset = harness.call(QStringLiteral("reset"));
    ASSERT_FALSE(reset.isError()) << reset.errorCode();
    EXPECT_FALSE(reset.structured().value(QStringLiteral("halted")).toBool());
    const qint64 cursor = reset.structured().value(QStringLiteral("log_cursor_end")).toInteger();
    EXPECT_GT(cursor, 0);
    EXPECT_LE(static_cast<std::uint64_t>(cursor), provider().logOf(device)->lastSeq());
    ASSERT_EQ(provider().record().markers.size(), 2U);
    EXPECT_TRUE(provider().record().markers[0].second.contains(QStringLiteral("reset app")));
    EXPECT_TRUE(provider().record().markers[1].second.contains(QStringLiteral("done in")));
    EXPECT_EQ(provider().record().busy.size(), 1U);
    EXPECT_EQ(provider().record().endBusy, 1);
}

TEST_F(JLinkTools, FlashFromAPath) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    const std::string path = imagePath(0x0, 8192);
    auto flash = harness.call(QStringLiteral("flash"),
                              args({{"path", QString::fromStdString(path)}}), 30s, true);
    ASSERT_FALSE(flash.isError()) << flash.errorCode() << " " << flash.errorDetail();
    const QJsonObject result = flash.structured();
    EXPECT_EQ(result.value(QStringLiteral("status")).toString(), QStringLiteral("ok"));
    EXPECT_EQ(result.value(QStringLiteral("target")).toString(), QStringLiteral("app"));
    EXPECT_EQ(result.value(QStringLiteral("file")).toString(), QString::fromStdString(path));
    EXPECT_EQ(result.value(QStringLiteral("chip")).toString(), QStringLiteral("nRF52840"));
    EXPECT_TRUE(result.contains(QStringLiteral("log_cursor_end")));
    EXPECT_GT(flash.progressCount(), 0U);
    ASSERT_EQ(harness.consent.requests.size(), 1U);
    EXPECT_EQ(harness.consent.requests[0].categories,
              hio::mcp::ConsentCategories(hio::mcp::ConsentCategory::Flash));
    EXPECT_EQ(harness.consent.requests[0].details.value(QStringLiteral("size")).toInteger(), 8192);
    ASSERT_EQ(provider().record().markers.size(), 2U);
    EXPECT_TRUE(provider().record().markers[0].second.contains(QStringLiteral("flash app.hex")));
    EXPECT_EQ(provider().record().busy.at(0).second, QStringLiteral("AI: Flashing…"));
    // The ConsoleLog is not cleared (ADR-0099 note): the boot line is still there.
    EXPECT_GT(provider().logOf(device)->size(hio::device::Channel::Shell), 0U);
    EXPECT_TRUE(harness.call(QStringLiteral("wait_for_connection"), args({{"timeout", 10}}))
                    .structured()
                    .value(QStringLiteral("connected"))
                    .toBool());
}

TEST_F(JLinkTools, FlashArgumentRules) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto both = harness.call(
        QStringLiteral("flash"),
        args({{"path", "/a.hex"}, {"catalog_id", "0123456789abcdef0123456789abcdef"}}));
    EXPECT_EQ(both.rpcError(), -32602);
    auto neither = harness.call(QStringLiteral("flash"));
    EXPECT_EQ(neither.rpcError(), -32602);
    // Review Q4: there is no `verify` argument (images are always verified); the schema rejects
    // unknown arguments.
    auto noVerify =
        harness.call(QStringLiteral("flash"), args({{"path", "/a.hex"}, {"verify", false}}));
    EXPECT_EQ(noVerify.rpcError(), -32602);
    EXPECT_EQ(noVerify.rpcPointer(), QStringLiteral("/arguments/verify"));
    const QJsonArray tools = harness.rpc(QStringLiteral("tools/list"))
                                 .value(QStringLiteral("result"))
                                 .toObject()
                                 .value(QStringLiteral("tools"))
                                 .toArray();
    bool listed = false;
    for (const auto& tool : tools) {
        if (tool.toObject().value(QStringLiteral("name")).toString() == QStringLiteral("flash")) {
            listed = true;
            EXPECT_FALSE(tool.toObject()
                             .value(QStringLiteral("inputSchema"))
                             .toObject()
                             .value(QStringLiteral("properties"))
                             .toObject()
                             .contains(QStringLiteral("verify")));
        }
    }
    EXPECT_TRUE(listed);
    auto relative = harness.call(QStringLiteral("flash"), args({{"path", "a.hex"}}));
    EXPECT_EQ(relative.errorCode(), "usage.invalid_argument");
    auto missing = harness.call(QStringLiteral("flash"), args({{"path", "/nonexistent/a.hex"}}));
    EXPECT_EQ(missing.errorCode(), "io.file_not_found");
    auto noCatalog = harness.call(QStringLiteral("flash"),
                                  args({{"catalog_id", "0123456789abcdef0123456789abcdef"}}));
    EXPECT_EQ(noCatalog.errorCode(), "usage.invalid_argument");
    EXPECT_TRUE(provider().record().markers.empty());
}

TEST_F(JLinkTools, FlashUicrImageNeedsAllowUicr) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    const std::string path = hexFile(
        provider().root() / "uicr.hex",
        {hio::firmware::Segment{.address = 0x0, .data = hio::firmware::test::filled(4096, 0x11)},
         hio::firmware::Segment{.address = 0x10001000,
                                .data = hio::firmware::test::filled(4, 0x22)}});
    auto refused =
        harness.call(QStringLiteral("flash"), args({{"path", QString::fromStdString(path)}}));
    EXPECT_EQ(refused.errorCode(), "firmware.uicr_in_image");
    ASSERT_EQ(harness.consent.requests.size(), 1U);
    EXPECT_EQ(harness.consent.requests[0].categories,
              hio::mcp::ConsentCategories(hio::mcp::ConsentCategory::Flash));

    auto allowed =
        harness.call(QStringLiteral("flash"),
                     args({{"path", QString::fromStdString(path)}, {"allow_uicr", true}}), 30s);
    EXPECT_FALSE(allowed.isError()) << allowed.errorCode() << " " << allowed.errorDetail();
    ASSERT_EQ(harness.consent.requests.size(), 2U);
    EXPECT_EQ(harness.consent.requests[1].categories,
              hio::mcp::ConsentCategory::Flash | hio::mcp::ConsentCategory::Uicr);
}

TEST_F(JLinkTools, FlashFromTheCatalog) {
    hio::firmware::test::HttpFixture http;
    ASSERT_TRUE(http.listening());
    const std::string id = "0123456789abcdef0123456789abcdef";
    auto image = hio::firmware::FirmwareImage::fromSegments(
        {hio::firmware::Segment{.address = 0, .data = hio::firmware::test::filled(4096, 0x33)}});
    http.set(QStringLiteral("/chester/%1/hex").arg(QString::fromStdString(id)),
             hio::firmware::test::HttpFixture::Response::ok(
                 QByteArray::fromStdString(hio::firmware::test::toHex(*image))));
    hio::firmware::FirmwareCache cache(provider().root() / "cache");
    hio::firmware::FirmwareCatalog catalog(
        hio::firmware::CatalogOptions{
            .baseUrl = QUrl(http.baseUrl()), .transferTimeout = 5s, .maxBodyBytes = 1024ULL * 1024},
        &cache);
    Harness harness(provider(), limits(), &catalog);
    connectDevice(harness);
    auto flash = harness.call(QStringLiteral("flash"),
                              args({{"catalog_id", QString::fromStdString(id)}}), 30s);
    ASSERT_FALSE(flash.isError()) << flash.errorCode() << " " << flash.errorDetail();
    EXPECT_EQ(flash.structured().value(QStringLiteral("catalog_id")).toString(),
              QString::fromStdString(id));
    EXPECT_EQ(harness.consent.requests.at(0).details.value(QStringLiteral("catalog_id")).toString(),
              QString::fromStdString(id));
}

TEST_F(JLinkTools, BusyCancelAndDeviceClosedMidFlash) {
    provider().scenario(
        QJsonDocument(scenario({}, QJsonObject{{QStringLiteral("downloadDelayMs"), 1500}}))
            .toJson());
    Harness harness(provider(), limits());
    connectDevice(harness);
    const QString path = QString::fromStdString(imagePath(0x0, 4096));

    auto flash = harness.start(QStringLiteral("flash"), args({{"path", path}}));
    ASSERT_TRUE(runUntil([&] { return !provider().record().busy.empty(); }));
    auto busy = harness.call(QStringLiteral("erase"));
    EXPECT_EQ(busy.errorCode(), "internal.busy");
    EXPECT_EQ(busy.errorDetail(), "flash");
    auto status = harness.call(QStringLiteral("status"));
    EXPECT_EQ(status.structured()
                  .value(QStringLiteral("devices"))
                  .toArray()
                  .at(0)
                  .toObject()
                  .value(QStringLiteral("busy_operation"))
                  .toString(),
              QStringLiteral("flash"));
    // The user presses Cancel on the busy tab (ADR-0105).
    ASSERT_TRUE(provider().record().busyCancel);
    provider().record().busyCancel();
    ASSERT_TRUE(runUntil([&] { return flash.done(); }, 30s));
    EXPECT_EQ(flash.errorCode(), "cancelled");
    EXPECT_TRUE(provider().record().markers.back().second.contains(QStringLiteral("cancelled")));
    EXPECT_TRUE(runUntil(
        [&] {
            return harness.call(QStringLiteral("status"))
                .structured()
                .value(QStringLiteral("devices"))
                .toArray()
                .at(0)
                .toObject()
                .value(QStringLiteral("busy_operation"))
                .isNull();
        },
        30s));

    // The device goes away while flashing (the GUI tab closes).
    auto second = harness.start(QStringLiteral("flash"), args({{"path", path}}));
    ASSERT_TRUE(runUntil([&] { return provider().record().busy.size() == 2; }));
    ASSERT_TRUE(provider().close(device, {}));
    ASSERT_TRUE(runUntil([&] { return second.done(); }, 30s));
    EXPECT_EQ(second.errorCode(), "mcp.device_not_open");
    EXPECT_EQ(provider().record().endBusy, 2);
}

TEST_F(JLinkTools, ClientCancelMidFlash) {
    provider().scenario(
        QJsonDocument(scenario({}, QJsonObject{{QStringLiteral("downloadDelayMs"), 1500}}))
            .toJson());
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto flash = harness.start(QStringLiteral("flash"),
                               args({{"path", QString::fromStdString(imagePath(0x0, 4096))}}));
    const int id = harness.lastId();
    ASSERT_TRUE(runUntil([&] { return !provider().record().busy.empty(); }));
    harness.cancel(id);
    ASSERT_TRUE(runUntil([&] { return provider().record().endBusy == 1; }, 30s));
    EXPECT_FALSE(flash.done()); // a cancelled call gets no response
    ASSERT_FALSE(harness.activity.entries.empty());
    EXPECT_EQ(harness.activity.entries.back().result, "cancelled");
}

TEST_F(JLinkTools, StalledOperationIsCancelledWithTimeout) {
    provider().scenario(
        QJsonDocument(scenario({}, QJsonObject{{QStringLiteral("downloadDelayMs"), 2000}}))
            .toJson());
    hio::mcp::Limits shrunk = limits();
    shrunk.tools.operationStall = 300ms;
    Harness harness(provider(), shrunk);
    connectDevice(harness);
    auto flash = harness.call(QStringLiteral("flash"),
                              args({{"path", QString::fromStdString(imagePath(0x0, 4096))}}), 30s);
    EXPECT_EQ(flash.errorCode(), "timeout");
    EXPECT_TRUE(provider().record().markers.back().second.contains(QStringLiteral("failed")));
}

TEST_F(JLinkTools, WriteFlashEraseRecover) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto write = harness.call(QStringLiteral("write_flash"),
                              args({{"address", "0x00010000"}, {"data_hex", "0102030405060708"}}),
                              30s, true);
    ASSERT_FALSE(write.isError()) << write.errorCode() << " " << write.errorDetail();
    EXPECT_EQ(write.structured().value(QStringLiteral("bytes")).toInteger(), 8);
    EXPECT_EQ(harness.consent.requests.back().categories,
              hio::mcp::ConsentCategories(hio::mcp::ConsentCategory::Flash));
    auto read = harness.call(QStringLiteral("read_memory"),
                             args({{"address", "0x00010000"}, {"length", 8}}));
    EXPECT_EQ(read.structured().value(QStringLiteral("hex")).toString(),
              QStringLiteral("0102030405060708"));

    // Review S2 / F4: UICR bytes without allow_uicr, the PIB and bytes outside flash and UICR are
    // refused before the consent request (checkFlashWrite): no request is recorded.
    const std::size_t requestsBefore = harness.consent.requests.size();
    auto uicr = harness.call(QStringLiteral("write_flash"),
                             args({{"address", "0x10001000"}, {"data_hex", "00"}}));
    EXPECT_EQ(uicr.errorCode(), "firmware.uicr_in_image");
    auto pib =
        harness.call(QStringLiteral("write_flash"),
                     args({{"address", "0x10001080"}, {"data_hex", "00"}, {"allow_uicr", true}}));
    EXPECT_EQ(pib.errorCode(), "firmware.out_of_range");
    auto outside = harness.call(QStringLiteral("write_flash"),
                                args({{"address", "0x20000000"}, {"data_hex", "00"}}));
    EXPECT_EQ(outside.errorCode(), "firmware.out_of_range");
    EXPECT_EQ(harness.consent.requests.size(), requestsBefore);
    // With allow_uicr: flash + uicr.
    auto allowed = harness.call(
        QStringLiteral("write_flash"),
        args({{"address", "0x10001200"}, {"data_hex", "00"}, {"allow_uicr", true}}), 30s);
    ASSERT_FALSE(allowed.isError()) << allowed.errorCode() << " " << allowed.errorDetail();
    EXPECT_EQ(harness.consent.requests.back().categories,
              hio::mcp::ConsentCategory::Flash | hio::mcp::ConsentCategory::Uicr);
    EXPECT_TRUE(harness.consent.requests.back().details.value(QStringLiteral("uicr")).toBool());
    // allow_uicr with bytes outside the UICR: flash only.
    auto plain = harness.call(
        QStringLiteral("write_flash"),
        args({{"address", "0x00010010"}, {"data_hex", "01"}, {"allow_uicr", true}}), 30s);
    ASSERT_FALSE(plain.isError()) << plain.errorCode() << " " << plain.errorDetail();
    EXPECT_EQ(harness.consent.requests.back().categories,
              hio::mcp::ConsentCategories(hio::mcp::ConsentCategory::Flash));

    // ADR-0161: erase scope=code needs erase; scope=all and recover need erase + uicr.
    auto erase = harness.call(QStringLiteral("erase"), {}, 30s);
    ASSERT_FALSE(erase.isError()) << erase.errorCode() << " " << erase.errorDetail();
    EXPECT_EQ(erase.structured().value(QStringLiteral("scope")).toString(), QStringLiteral("code"));
    EXPECT_EQ(harness.consent.requests.back().categories,
              hio::mcp::ConsentCategories(hio::mcp::ConsentCategory::Erase));
    auto eraseAll = harness.call(QStringLiteral("erase"), args({{"scope", "all"}}), 30s);
    ASSERT_FALSE(eraseAll.isError()) << eraseAll.errorCode() << " " << eraseAll.errorDetail();
    EXPECT_EQ(harness.consent.requests.back().categories,
              hio::mcp::ConsentCategory::Erase | hio::mcp::ConsentCategory::Uicr);
    auto recover = harness.call(QStringLiteral("recover"), {}, 60s);
    ASSERT_FALSE(recover.isError()) << recover.errorCode() << " " << recover.errorDetail();
    EXPECT_TRUE(recover.structured().contains(QStringLiteral("duration")));
    EXPECT_EQ(harness.consent.requests.back().categories,
              hio::mcp::ConsentCategory::Erase | hio::mcp::ConsentCategory::Uicr);
}

// ADR-0161 with the CLI broker rule: a server allowed to erase only cannot erase everything.
TEST_F(JLinkTools, EraseAllNeedsUicrConsent) {
    Harness harness(provider(), limits());
    connectDevice(harness);
    harness.consent.mode = hio::mcp::test::FakeConsentBroker::Mode::Deny;
    auto eraseAll = harness.call(QStringLiteral("erase"), args({{"scope", "all"}}), 30s);
    EXPECT_EQ(eraseAll.errorCode(), "mcp.tool_not_allowed");
    EXPECT_EQ(harness.consent.requests.back().categories,
              hio::mcp::ConsentCategory::Erase | hio::mcp::ConsentCategory::Uicr);
    EXPECT_EQ(harness.consent.requests.back().details.value(QStringLiteral("scope")).toString(),
              QStringLiteral("all"));
}

TEST_F(JLinkTools, MissingOptionalExport) {
    provider().settings().library = HIO_FAKE_JLINK_BASIC_LIBRARY;
    Harness harness(provider(), limits());
    connectDevice(harness);
    auto status = harness.call(QStringLiteral("target_status"));
    EXPECT_EQ(status.errorCode(), "dependency.jlink_function_missing");
}

TEST_F(JLinkTools, BleSession) {
    Harness harness(provider(), limits());
    auto connect =
        harness.call(QStringLiteral("connect"), args({{"device", "ble:F1:22:33:44:55:66"}}));
    ASSERT_FALSE(connect.isError()) << connect.errorCode() << " " << connect.errorDetail();
    ASSERT_TRUE(harness.call(QStringLiteral("wait_for_connection"), args({{"timeout", 10}}))
                    .structured()
                    .value(QStringLiteral("connected"))
                    .toBool());
    auto command = harness.call(QStringLiteral("send_command"), args({{"command", "version"}}));
    ASSERT_FALSE(command.isError()) << command.errorCode();
    EXPECT_EQ(command.structured().value(QStringLiteral("end")).toString(),
              QStringLiteral("prompt"));
    EXPECT_EQ(command.structured().value(QStringLiteral("output")).toArray().size(), 1);
    for (const char* tool : {"target_status", "stop", "release_probe", "read_pib"}) {
        EXPECT_EQ(harness.call(QString::fromLatin1(tool)).errorCode(), "mcp.capability_missing")
            << tool;
    }
}

} // namespace
} // namespace hio::mcp::tools::test
