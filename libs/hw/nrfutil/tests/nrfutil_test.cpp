// nRF Util locator, version check and modem updater (plan/features/0003, D1 and D2) against the
// test-only fake nRF Util (tests/fake_nrfutil).

#include "hwio/core/error.h"
#include "hwio/firmware/program_progress.h"
#include "hwio/hw/nrfutil/nrfutil.h"

#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QString>
#include <QTemporaryDir>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hwio::ErrorCode;
using hwio::firmware::ProgramPhase;
using hwio::firmware::ProgramProgress;
using hwio::nrfutil::LocatorOptions;
using hwio::nrfutil::ModemUpdaterTiming;
using hwio::nrfutil::NrfUtilModemUpdater;
using hwio::nrfutil::Version;

const fs::path fake{HWIO_FAKE_NRFUTIL};

class NrfUtil : public ::testing::Test {
protected:
    void SetUp() override {
        qputenv("HWIO_FAKE_NRFUTIL_LOG", m_dir.filePath(QStringLiteral("args.log")).toUtf8());
        qputenv("HWIO_FAKE_NRFUTIL_STEP_MS", "5");
    }
    void TearDown() override {
        qunsetenv("HWIO_FAKE_NRFUTIL_MODE");
        qunsetenv("HWIO_FAKE_NRFUTIL_LOG");
        qunsetenv("HWIO_FAKE_NRFUTIL_STEP_MS");
    }
    static void mode(const char* value) { qputenv("HWIO_FAKE_NRFUTIL_MODE", value); }
    [[nodiscard]] std::string arguments() const {
        std::ifstream in(m_dir.filePath(QStringLiteral("args.log")).toStdString());
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
    [[nodiscard]] fs::path dir() const { return {m_dir.path().toStdU16String()}; }

private:
    QTemporaryDir m_dir;
};

TEST(NrfUtilVersion, Parse) {
    EXPECT_EQ(hwio::nrfutil::parseVersion("nrfutil 7.13.0 (abc 2024-10-01)"),
              (Version{.major = 7, .minor = 13, .patch = 0}));
    EXPECT_EQ(hwio::nrfutil::parseVersion("nrfutil-device 2.8.1"),
              (Version{.major = 2, .minor = 8, .patch = 1}));
    EXPECT_FALSE(hwio::nrfutil::parseVersion("no version"));
    EXPECT_LT((Version{.major = 6, .minor = 9, .patch = 9}), hwio::nrfutil::minimumCoreVersion);
    EXPECT_EQ(hwio::nrfutil::formatVersion({.major = 1, .minor = 2, .patch = 3}), "1.2.3");
}

TEST_F(NrfUtil, LocatorPrecedence) {
    const fs::path missing = dir() / "missing" / "nrfutil";
    LocatorOptions options;
    options.searchPath = {fake.parent_path()};
    // Discovery through the PATH.
    auto found = hwio::nrfutil::locate(options);
    ASSERT_TRUE(found) << found.error().detail;
    EXPECT_EQ(*found, fake);
    // A setting wins over discovery; an override that does not exist is an error.
    options.setting = missing;
    auto setting = hwio::nrfutil::locate(options);
    ASSERT_FALSE(setting);
    EXPECT_EQ(setting.error().code, ErrorCode::DependencyNrfUtilNotFound);
    options.environment = fake;
    EXPECT_EQ(*hwio::nrfutil::locate(options), fake); // environment > setting
    options.option = missing;
    EXPECT_FALSE(hwio::nrfutil::locate(options)); // option > environment
    // Nothing found: the searched paths are listed.
    LocatorOptions empty;
    empty.searchPath = {dir() / "a"};
    empty.installDirs = {dir() / "b"};
    const auto none = hwio::nrfutil::locate(empty);
    ASSERT_FALSE(none);
    EXPECT_EQ(none.error().code, ErrorCode::DependencyNrfUtilNotFound);
    EXPECT_NE(none.error().detail.find((dir() / "b").string()), std::string::npos);
    // Install directories after the PATH.
    empty.installDirs = {fake.parent_path()};
    EXPECT_EQ(*hwio::nrfutil::locate(empty), fake);
}

TEST_F(NrfUtil, DefaultLocatorOptionsUseTheEnvironment) {
    qputenv("HWIO_NRFUTIL", "/some/nrfutil");
    const LocatorOptions options = hwio::nrfutil::defaultLocatorOptions(std::nullopt, nullptr);
    qunsetenv("HWIO_NRFUTIL");
    ASSERT_TRUE(options.environment.has_value());
    EXPECT_EQ(options.environment.value_or(fs::path()), fs::path("/some/nrfutil"));
    EXPECT_FALSE(options.installDirs.empty());
}

TEST_F(NrfUtil, VersionCheck) {
    const auto info = hwio::nrfutil::checkVersions(fake);
    ASSERT_TRUE(info) << info.error().detail;
    EXPECT_EQ(info->core, (Version{.major = 7, .minor = 13, .patch = 0}));
    EXPECT_EQ(info->device, (Version{.major = 2, .minor = 8, .patch = 1}));
    mode("old");
    const auto old = hwio::nrfutil::checkVersions(fake);
    ASSERT_FALSE(old);
    EXPECT_EQ(old.error().code, ErrorCode::DependencyNrfUtilTooOld);
    EXPECT_EQ(old.error().message, "nRF Util 6.1.0 is too old.");
    mode("nodevice");
    const auto noDevice = hwio::nrfutil::checkVersions(fake);
    ASSERT_FALSE(noDevice);
    EXPECT_EQ(noDevice.error().code, ErrorCode::DependencyNrfUtilDeviceMissing);
    const auto missing = hwio::nrfutil::checkVersions(dir() / "nope");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, ErrorCode::DependencyNrfUtilNotFound);
}

TEST_F(NrfUtil, AVersionProbeStopsOnRequest) {
    mode("slowversion"); // `--version` answers after 30 s
    std::stop_source stop;
    std::jthread stopper([&stop] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        stop.request_stop();
    });
    const auto before = std::chrono::steady_clock::now();
    const auto info =
        hwio::nrfutil::checkVersions(fake, std::chrono::seconds(20), stop.get_token());
    EXPECT_LT(std::chrono::steady_clock::now() - before, std::chrono::seconds(5));
    ASSERT_FALSE(info);
    EXPECT_EQ(info.error().code, ErrorCode::Cancelled);
}

TEST_F(NrfUtil, AVersionProbeTimesOut) {
    mode("slowversion");
    const auto info = hwio::nrfutil::checkVersions(fake, std::chrono::milliseconds(300));
    ASSERT_FALSE(info);
    EXPECT_EQ(info.error().code, ErrorCode::Timeout);
}

TEST_F(NrfUtil, ProgramArguments) {
    const auto args = NrfUtilModemUpdater::programArguments("/tmp/modem.zip", 801234567);
    const std::vector<std::string> expected{"device",         "program",         "--firmware",
                                            "/tmp/modem.zip", "--serial-number", "000801234567",
                                            "--json"};
    EXPECT_EQ(args, expected);
}

TEST_F(NrfUtil, UpdateReportsProgress) {
    NrfUtilModemUpdater updater(fake);
    std::vector<ProgramProgress> progress;
    const auto status = updater.updateModem(
        "/tmp/mfw.zip", 801234567, [&](const ProgramProgress& p) { progress.push_back(p); }, {});
    ASSERT_TRUE(status) << status.error().detail;
    ASSERT_GE(progress.size(), 3U);
    EXPECT_EQ(progress.front().phase, ProgramPhase::Modem);
    EXPECT_EQ(progress.back().done, 100U);
    EXPECT_NE(arguments().find("device program --firmware /tmp/mfw.zip --serial-number "
                               "000801234567 --json"),
              std::string::npos)
        << arguments();
}

TEST_F(NrfUtil, UpdateFailure) {
    mode("fail");
    NrfUtilModemUpdater updater(fake);
    const auto status = updater.updateModem("/tmp/mfw.zip", 1, {}, {});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::FirmwareModemUpdateFailed);
    EXPECT_NE(status.error().detail.find("Modem DFU failed (simulated)"), std::string::npos)
        << status.error().detail;
}

TEST_F(NrfUtil, HangTimesOutAndIsKilled) {
    mode("hang");
    NrfUtilModemUpdater updater(fake,
                                ModemUpdaterTiming{.overallTimeout = std::chrono::milliseconds(300),
                                                   .terminateGrace = std::chrono::milliseconds(500),
                                                   .poll = std::chrono::milliseconds(20),
                                                   .maxOutputBytes = 1024ULL * 1024});
    const auto started = std::chrono::steady_clock::now();
    const auto status = updater.updateModem("/tmp/mfw.zip", 1, {}, {});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::Timeout);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
}

TEST_F(NrfUtil, Cancel) {
    mode("hang");
    NrfUtilModemUpdater updater(fake);
    std::stop_source stop;
    std::jthread canceller([&stop] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        stop.request_stop();
    });
    const auto status = updater.updateModem("/tmp/mfw.zip", 1, {}, stop.get_token());
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::Cancelled);
}

TEST_F(NrfUtil, OutputFloodIsCapped) {
    mode("flood");
    NrfUtilModemUpdater updater(fake, ModemUpdaterTiming{.overallTimeout = std::chrono::minutes(1),
                                                         .terminateGrace = std::chrono::seconds(5),
                                                         .poll = std::chrono::milliseconds(20),
                                                         .maxOutputBytes = 64ULL * 1024});
    // The success line comes after the cap: it is dropped, the exit code decides.
    const auto status = updater.updateModem("/tmp/mfw.zip", 1, {}, {});
    EXPECT_TRUE(status) << status.error().detail;
}

TEST(NrfUtilJson, ToleratesUnknownFields) {
    const auto parse = [](const char* text) {
        return hwio::nrfutil::parseJsonEvent(QJsonDocument::fromJson(text).object());
    };
    const auto progress =
        parse(R"({"type":"task_progress","data":{"progress":{"progressPercentage":42},"x":1}})");
    EXPECT_EQ(progress.percent, 42);
    EXPECT_FALSE(progress.success);
    const auto failed =
        parse(R"({"type":"task_end","data":{"result":"fail","error":{"description":"bad"}}})");
    EXPECT_EQ(failed.success, false);
    EXPECT_EQ(failed.message, "bad");
    const auto ok = parse(R"({"type":"task_end","data":{"result":"success"}})");
    EXPECT_EQ(ok.success, true);
    const auto other = parse(R"({"what":"ever"})");
    EXPECT_FALSE(other.percent);
    EXPECT_FALSE(other.success);
}

} // namespace
