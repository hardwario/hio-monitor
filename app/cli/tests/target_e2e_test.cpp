// End-to-end tests of the M6 target commands (plan/features/0013 B4): the real `hio` executable
// with the fake J-Link library and its RAM / register / zone model (each run is a new process, so
// the fake target starts running and with zeroed RAM every time).

#include "cli_process.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QtContainerFwd>

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <vector>

namespace {

using hio::cli::test::CliProcess;
using hio::cli::test::RunResult;

std::vector<QJsonObject> envelopes(const std::string& out) {
    std::vector<QJsonObject> result;
    for (const QByteArray& line : QByteArray::fromStdString(out).split('\n')) {
        if (!line.isEmpty()) {
            result.push_back(QJsonDocument::fromJson(line).object());
        }
    }
    return result;
}

QJsonObject lastData(const RunResult& result) {
    const auto all = envelopes(result.out);
    return all.empty() ? QJsonObject() : all.back().value(QStringLiteral("data")).toObject();
}

QString errorCode(const RunResult& result) {
    const auto all = envelopes(result.out);
    return all.empty() ? QString()
                       : all.back()
                             .value(QStringLiteral("error"))
                             .toObject()
                             .value(QStringLiteral("code"))
                             .toString();
}

QStringList args(std::initializer_list<const char*> list) {
    QStringList result;
    for (const char* item : list) {
        result.append(QString::fromUtf8(item));
    }
    return result;
}

// "Hello" at flash 0x1000 (4096); 256 KiB RAM at 0x20000000 (536870912).
constexpr const char* scenario = R"({"probes": [
    {"serialNumber": 801000001, "product": "Test Probe",
     "chip": {"memory": [{"address": 4096, "hex": "48656c6c6f"}],
              "ram": [{"address": 536870912, "size": 262144}],
              "registers": [{"name": "R0", "value": 1}, {"name": "PC", "value": 4660}],
              "zones": [{"name": "AHB-AP", "description": "Memory via AHB-AP",
                         "virtualAddress": 0}],
              "coreId": 732959863}},
    {"serialNumber": 801000002, "product": "Test Probe 2", "chip": {"family": "nrf91"}}]})";

class TargetCli : public CliProcess {
protected:
    void SetUp() override { writeFile(QStringLiteral("scenario.json"), scenario); }

    void writeFile(const QString& name, const QByteArray& bytes) const {
        QFile file(path(name));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        ASSERT_EQ(file.write(bytes), bytes.size());
    }
    [[nodiscard]] QString path(const QString& name) const {
        return home() + QLatin1Char('/') + name;
    }

    [[nodiscard]] QProcessEnvironment fakeEnvironment(const char* library) const {
        QProcessEnvironment env = environment();
        env.insert(QStringLiteral("HIO_FAKE_JLINK_SCENARIO"),
                   path(QStringLiteral("scenario.json")));
        env.insert(QStringLiteral("HIO_JLINK_LIBRARY"), QString::fromUtf8(library));
        return env;
    }

    // The app target of probe 801000001 (`chester app -n 801000001 ...`).
    [[nodiscard]] RunResult app(const QStringList& command, bool json = false) const {
        QStringList all;
        if (json) {
            all.append(QStringLiteral("--json"));
        }
        all << QStringLiteral("chester") << QStringLiteral("app") << QStringLiteral("-n")
            << QStringLiteral("801000001") << command;
        return run(all, fakeEnvironment(HIO_FAKE_JLINK_LIBRARY));
    }
};

TEST_F(TargetCli, StatusHumanAndJson) {
    const RunResult human = app(args({"status"}));
    ASSERT_EQ(human.exitCode, 0) << human.err;
    EXPECT_NE(human.out.find("CPU: running\nCore ID: 0x2BB01477\nJ-Link device: "),
              std::string::npos)
        << human.out;
    EXPECT_NE(human.err.find("Using J-Link 801000001 (Test Probe)"), std::string::npos)
        << human.err;

    const RunResult json = app(args({"status"}), true);
    ASSERT_EQ(json.exitCode, 0) << json.out << json.err;
    ASSERT_EQ(envelopes(json.out).size(), 1U) << json.out;
    const QJsonObject data = lastData(json);
    EXPECT_EQ(data.keys(), (QStringList{QStringLiteral("coreId"), QStringLiteral("device"),
                                        QStringLiteral("halted")}));
    EXPECT_FALSE(data.value(QStringLiteral("halted")).toBool());
    EXPECT_EQ(data.value(QStringLiteral("coreId")).toString(), QStringLiteral("0x2BB01477"));
    EXPECT_FALSE(data.value(QStringLiteral("device")).toString().isEmpty());
}

TEST_F(TargetCli, HaltAndGo) {
    const RunResult halt = app(args({"halt"}));
    ASSERT_EQ(halt.exitCode, 0) << halt.err;
    EXPECT_EQ(halt.out, "CPU halted\n");
    const RunResult haltJson = app(args({"halt"}), true);
    ASSERT_EQ(haltJson.exitCode, 0) << haltJson.out;
    EXPECT_TRUE(lastData(haltJson).value(QStringLiteral("halted")).toBool());

    const RunResult go = app(args({"go"}));
    ASSERT_EQ(go.exitCode, 0) << go.err;
    EXPECT_EQ(go.out, "CPU running\n");
    const RunResult goJson = app(args({"go"}), true);
    ASSERT_EQ(goJson.exitCode, 0) << goJson.out;
    const QJsonObject data = lastData(goJson);
    ASSERT_TRUE(data.contains(QStringLiteral("halted")));
    EXPECT_FALSE(data.value(QStringLiteral("halted")).toBool());
}

// The fake target runs after every connect: registers need a halted core (exit 6).
TEST_F(TargetCli, RegsOnARunningCore) {
    const RunResult human = app(args({"regs"}));
    EXPECT_EQ(human.exitCode, 6) << human.err;
    EXPECT_NE(human.err.find("The CPU is running."), std::string::npos) << human.err;
    const RunResult json = app(args({"regs"}), true);
    EXPECT_EQ(json.exitCode, 6);
    EXPECT_EQ(errorCode(json), QStringLiteral("target.not_halted"));
}

TEST_F(TargetCli, Zones) {
    const RunResult human = app(args({"zones"}));
    ASSERT_EQ(human.exitCode, 0) << human.err;
    EXPECT_EQ(human.out, "AHB-AP  0x00000000  Memory via AHB-AP\n");
    const RunResult json = app(args({"zones"}), true);
    ASSERT_EQ(json.exitCode, 0) << json.out;
    const QJsonArray zones = lastData(json).value(QStringLiteral("zones")).toArray();
    ASSERT_EQ(zones.size(), 1);
    EXPECT_EQ(zones.at(0).toObject().value(QStringLiteral("name")).toString(),
              QStringLiteral("AHB-AP"));
}

// The lte target (nRF91 probe): no zones reported.
TEST_F(TargetCli, LteStatusAndNoZones) {
    const auto lte = [this](const char* command, bool json) {
        QStringList all;
        if (json) {
            all.append(QStringLiteral("--json"));
        }
        all << QStringLiteral("chester") << QStringLiteral("lte") << QStringLiteral("-n")
            << QStringLiteral("801000002") << QString::fromUtf8(command);
        return run(all, fakeEnvironment(HIO_FAKE_JLINK_LIBRARY));
    };
    const RunResult status = lte("status", true);
    ASSERT_EQ(status.exitCode, 0) << status.out << status.err;
    EXPECT_FALSE(lastData(status).value(QStringLiteral("halted")).toBool());
    const RunResult human = lte("zones", false);
    ASSERT_EQ(human.exitCode, 0) << human.err;
    EXPECT_EQ(human.out, "No memory zones.\n");
    const RunResult json = lte("zones", true);
    ASSERT_EQ(json.exitCode, 0) << json.out;
    ASSERT_TRUE(lastData(json).value(QStringLiteral("zones")).isArray());
    EXPECT_TRUE(lastData(json).value(QStringLiteral("zones")).toArray().isEmpty());
}

TEST_F(TargetCli, MemReadHexdumpAndJson) {
    const RunResult human = app(args({"mem", "read", "0x1000", "-l", "8"}));
    ASSERT_EQ(human.exitCode, 0) << human.err;
    EXPECT_EQ(human.out,
              "0x00001000  48 65 6c 6c 6f ff ff ff" + std::string(27, ' ') + "|Hello...|\n");
    const RunResult words = app(args({"mem", "read", "4096", "-l", "8", "-w", "32"}));
    ASSERT_EQ(words.exitCode, 0) << words.err;
    EXPECT_EQ(words.out, "0x00001000  6c6c6548 ffffff6f" + std::string(21, ' ') + "|Hello...|\n");

    const RunResult json = app(args({"mem", "read", "0x1000", "-l", "0x8"}), true);
    ASSERT_EQ(json.exitCode, 0) << json.out;
    const QJsonObject data = lastData(json);
    EXPECT_EQ(data.value(QStringLiteral("address")).toString(), QStringLiteral("0x00001000"));
    EXPECT_EQ(data.value(QStringLiteral("length")).toInt(), 8);
    EXPECT_EQ(data.value(QStringLiteral("width")).toInt(), 8);
    EXPECT_EQ(data.value(QStringLiteral("hex")).toString(), QStringLiteral("48656c6c6fffffff"));

    // Default length 64 bytes: 4 lines.
    const RunResult defaults = app(args({"mem", "read", "0x20000000"}));
    ASSERT_EQ(defaults.exitCode, 0) << defaults.err;
    EXPECT_EQ(QByteArray::fromStdString(defaults.out).count('\n'), 4);
}

// Three 64 KiB operations in one session into a new file.
TEST_F(TargetCli, MemReadToFileInChunks) {
    const QString file = path(QStringLiteral("ram.bin"));
    const RunResult json = app(QStringList{QStringLiteral("mem"), QStringLiteral("read"),
                                           QStringLiteral("0x20000000"), QStringLiteral("-l"),
                                           QStringLiteral("0x30000"), QStringLiteral("-o"), file},
                               true);
    ASSERT_EQ(json.exitCode, 0) << json.out << json.err;
    QFile saved(file);
    ASSERT_TRUE(saved.open(QIODevice::ReadOnly));
    const QByteArray content = saved.readAll();
    EXPECT_EQ(content, QByteArray(0x30000, '\0'));
    const QJsonObject data = lastData(json);
    EXPECT_EQ(data.value(QStringLiteral("file")).toString(), file);
    EXPECT_EQ(data.value(QStringLiteral("length")).toInt(), 0x30000);
    EXPECT_EQ(
        data.value(QStringLiteral("sha256")).toString(),
        QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex()));
    EXPECT_FALSE(data.contains(QStringLiteral("hex")));

    const QString other = path(QStringLiteral("flash.bin"));
    const RunResult human =
        app(QStringList{QStringLiteral("mem"), QStringLiteral("read"), QStringLiteral("0x1000"),
                        QStringLiteral("-l"), QStringLiteral("5"), QStringLiteral("-o"), other});
    ASSERT_EQ(human.exitCode, 0) << human.err;
    EXPECT_EQ(human.out, "Read 5 bytes from 0x00001000 into " + other.toStdString() + "\n");
    QFile flash(other);
    ASSERT_TRUE(flash.open(QIODevice::ReadOnly));
    EXPECT_EQ(flash.readAll(), QByteArray("Hello"));
}

TEST_F(TargetCli, MemReadNeverOverwritesAFile) {
    writeFile(QStringLiteral("exists.bin"), "keep");
    const QString file = path(QStringLiteral("exists.bin"));
    const RunResult json = app(QStringList{QStringLiteral("mem"), QStringLiteral("read"),
                                           QStringLiteral("0x1000"), QStringLiteral("-o"), file},
                               true);
    EXPECT_EQ(json.exitCode, 3);
    EXPECT_EQ(errorCode(json), QStringLiteral("io.file_exists"));
    EXPECT_EQ(envelopes(json.out)
                  .back()
                  .value(QStringLiteral("error"))
                  .toObject()
                  .value(QStringLiteral("detail"))
                  .toString(),
              file);
    QFile kept(file);
    ASSERT_TRUE(kept.open(QIODevice::ReadOnly));
    EXPECT_EQ(kept.readAll(), QByteArray("keep"));
}

// A failed read removes the file it created. Reads are unrestricted (ADR-0160): an unmapped
// address fails in the J-Link read (target.memory_access_failed).
TEST_F(TargetCli, MemReadFailureRemovesTheNewFile) {
    const QString file = path(QStringLiteral("unmapped.bin"));
    const RunResult json =
        app(QStringList{QStringLiteral("mem"), QStringLiteral("read"), QStringLiteral("0x30000000"),
                        QStringLiteral("-o"), file},
            true);
    EXPECT_NE(json.exitCode, 0);
    EXPECT_EQ(errorCode(json), QStringLiteral("target.memory_access_failed")) << json.out;
    EXPECT_FALSE(QFile::exists(file));
}

TEST_F(TargetCli, MemReadUsageErrors) {
    for (const QStringList& command :
         {args({"mem", "read", "0x1000", "-l", "0"}),
          args({"mem", "read", "0x1000", "-l", "3", "-w", "16"}),
          args({"mem", "read", "0x1001", "-l", "4", "-w", "32"}),
          args({"mem", "read", "0x1000", "-l", "0x1000001"}), args({"mem", "read", "x"}),
          args({"mem", "read", "0xFFFFFFF0", "-l", "32"})}) {
        const RunResult json = app(command, true);
        EXPECT_EQ(json.exitCode, 2) << command.join(QLatin1Char(' ')).toStdString();
        EXPECT_EQ(errorCode(json), QStringLiteral("usage.invalid_argument"));
    }
}

TEST_F(TargetCli, MemWriteNeedsConfirmation) {
    const RunResult json = app(args({"mem", "write", "0x20000000", "01020304"}), true);
    EXPECT_EQ(json.exitCode, 2);
    EXPECT_EQ(errorCode(json), QStringLiteral("usage.confirmation_required"));
    const RunResult human = app(args({"mem", "write", "0x20000000", "01020304"}));
    EXPECT_EQ(human.exitCode, 2) << human.err;
}

TEST_F(TargetCli, MemWriteHumanAndJson) {
    const RunResult human = app(args({"mem", "write", "0x20000000", "01020304", "--yes"}));
    ASSERT_EQ(human.exitCode, 0) << human.err;
    EXPECT_EQ(human.out, "Wrote 4 bytes to 0x20000000\n");

    writeFile(QStringLiteral("data.bin"), QByteArray(8, '\x5a'));
    const RunResult json =
        app(QStringList{QStringLiteral("mem"), QStringLiteral("write"), QStringLiteral("536870912"),
                        QStringLiteral("-i"), path(QStringLiteral("data.bin")),
                        QStringLiteral("-w"), QStringLiteral("32"), QStringLiteral("--yes")},
            true);
    ASSERT_EQ(json.exitCode, 0) << json.out << json.err;
    const QJsonObject data = lastData(json);
    EXPECT_EQ(data.value(QStringLiteral("address")).toString(), QStringLiteral("0x20000000"));
    EXPECT_EQ(data.value(QStringLiteral("length")).toInt(), 8);
    EXPECT_EQ(data.value(QStringLiteral("width")).toInt(), 32);
}

TEST_F(TargetCli, MemWriteOutsideRamAndPeripherals) {
    const RunResult json = app(args({"mem", "write", "0x1000", "01", "--yes"}), true);
    EXPECT_EQ(json.exitCode, 6) << json.out;
    EXPECT_EQ(errorCode(json), QStringLiteral("target.address_out_of_range"));
}

// Review S8: the range is checked before the confirmation question (no --yes, no TTY).
TEST_F(TargetCli, MemWriteRangeIsCheckedBeforeConfirmation) {
    const RunResult json = app(args({"mem", "write", "0x1000", "01"}), true);
    EXPECT_EQ(json.exitCode, 6) << json.out;
    EXPECT_EQ(errorCode(json), QStringLiteral("target.address_out_of_range"));
    const RunResult human = app(args({"mem", "write", "0x1000", "01"}));
    EXPECT_EQ(human.exitCode, 6) << human.err;
    EXPECT_EQ(human.err.find("Continue?"), std::string::npos) << human.err;
}

// ADR-0164: the SCS (VTOR) and the FPB are refused before the confirmation; the help says that
// a memory write can still run code.
TEST_F(TargetCli, MemWriteDeniesTheSystemControlSpaceAndDocumentsTheRisk) {
    for (const char* address : {"0xE000ED08", "0xE0002000"}) {
        const RunResult json = app(args({"mem", "write", address, "00000000"}), true);
        EXPECT_EQ(json.exitCode, 6) << address << json.out;
        EXPECT_EQ(errorCode(json), QStringLiteral("target.address_out_of_range")) << address;
    }
    const RunResult help = app(args({"mem", "write", "--help"}));
    EXPECT_EQ(help.exitCode, 0) << help.err;
    EXPECT_NE((help.out + help.err).find("run arbitrary code"), std::string::npos) << help.out;
}

TEST_F(TargetCli, MemWriteUsageErrors) {
    for (const QStringList& command :
         {args({"mem", "write", "0x20000000", "--yes"}),
          args({"mem", "write", "0x20000000", "010", "--yes"}),
          args({"mem", "write", "0x20000000", "010203", "-w", "16", "--yes"}),
          args({"mem", "write", "0x20000000", "01", "-i", "data.bin", "--yes"})}) {
        const RunResult json = app(command, true);
        EXPECT_EQ(json.exitCode, 2) << command.join(QLatin1Char(' ')).toStdString();
        EXPECT_EQ(errorCode(json), QStringLiteral("usage.invalid_argument"));
    }
}

TEST_F(TargetCli, FlashWriteNeedsConfirmation) {
    const RunResult json = app(args({"flash-write", "0x2000", "0102"}), true);
    EXPECT_EQ(json.exitCode, 2);
    EXPECT_EQ(errorCode(json), QStringLiteral("usage.confirmation_required"));
}

TEST_F(TargetCli, FlashWriteHumanAndJson) {
    const RunResult human = app(args({"flash-write", "0x2000", "0102", "--yes"}));
    ASSERT_EQ(human.exitCode, 0) << human.err;
    EXPECT_EQ(human.out, "Successfully completed\n");
    EXPECT_NE(human.err.find("Programming..."), std::string::npos) << human.err;

    writeFile(QStringLiteral("page.bin"), QByteArray(16, '\x11'));
    const RunResult json = app(QStringList{QStringLiteral("flash-write"), QStringLiteral("0x3000"),
                                           QStringLiteral("-i"), path(QStringLiteral("page.bin")),
                                           QStringLiteral("--yes")},
                               true);
    ASSERT_EQ(json.exitCode, 0) << json.out << json.err;
    const QJsonObject data = lastData(json);
    EXPECT_EQ(data.value(QStringLiteral("type")).toString(), QStringLiteral("result"));
    EXPECT_EQ(data.value(QStringLiteral("target")).toString(), QStringLiteral("app"));
    EXPECT_EQ(data.value(QStringLiteral("address")).toString(), QStringLiteral("0x00003000"));
    EXPECT_EQ(data.value(QStringLiteral("length")).toInt(), 16);
}

TEST_F(TargetCli, FlashWriteOutsideFlash) {
    const RunResult json = app(args({"flash-write", "0x20000000", "01", "--yes"}), true);
    EXPECT_EQ(json.exitCode, 7) << json.out;
    EXPECT_EQ(errorCode(json), QStringLiteral("firmware.out_of_range"));
}

// Review S8: flash-write checks its range (flash/UICR, PIB, --allow-uicr) before asking.
TEST_F(TargetCli, FlashWriteRangeIsCheckedBeforeConfirmation) {
    const RunResult outside = app(args({"flash-write", "0x20000000", "01"}), true);
    EXPECT_EQ(outside.exitCode, 7) << outside.out;
    EXPECT_EQ(errorCode(outside), QStringLiteral("firmware.out_of_range"));
    // The CHESTER app UICR customer area at 0x10001080 without --allow-uicr.
    const RunResult uicr = app(args({"flash-write", "0x10001200", "01"}), true);
    EXPECT_EQ(errorCode(uicr), QStringLiteral("firmware.uicr_in_image")) << uicr.out;
    // In range: the confirmation is asked (and refused without a TTY).
    const RunResult inside = app(args({"flash-write", "0x2000", "01"}), true);
    EXPECT_EQ(errorCode(inside), QStringLiteral("usage.confirmation_required"));
}

// Review Q10: `regs` on a halted core (fake scenario key chip.halted), human and --json.
TEST_F(TargetCli, RegsOnAHaltedCore) {
    writeFile(QStringLiteral("scenario.json"), R"({"probes": [
        {"serialNumber": 801000001, "product": "Test Probe",
         "chip": {"halted": true,
                  "registers": [{"name": "R0", "value": 1}, {"name": "PC", "value": 4660}]}}]})");
    const RunResult human = app(args({"regs"}));
    ASSERT_EQ(human.exitCode, 0) << human.err;
    EXPECT_EQ(human.out, "R0  0x00000001\nPC  0x00001234\n");
    const RunResult json = app(args({"regs"}), true);
    ASSERT_EQ(json.exitCode, 0) << json.out << json.err;
    const QJsonObject registers = lastData(json).value(QStringLiteral("registers")).toObject();
    EXPECT_EQ(registers.value(QStringLiteral("R0")).toString(), QStringLiteral("0x00000001"));
    EXPECT_EQ(registers.value(QStringLiteral("PC")).toString(), QStringLiteral("0x00001234"));
    EXPECT_EQ(registers.size(), 2);
}

// ADR-0151: a J-Link library without the optional functions.
TEST_F(TargetCli, MissingOptionalJLinkFunction) {
    const RunResult json = run(QStringList{QStringLiteral("--json"), QStringLiteral("chester"),
                                           QStringLiteral("app"), QStringLiteral("status")},
                               fakeEnvironment(HIO_FAKE_JLINK_BASIC_LIBRARY));
    EXPECT_EQ(json.exitCode, 4) << json.out << json.err;
    EXPECT_EQ(errorCode(json), QStringLiteral("dependency.jlink_function_missing"));
}

} // namespace
