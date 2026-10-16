// The environment checks against the fake J-Link library and the fake nRF Util
// (plan/features/0007, A2-A4).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/probe_lock.h"
#include "hio/diag/check.h"
#include "hio/diag/environment.h"
#include "hio/diag/process_scan.h"

#include <QFile>
#include <QIODevice>
#include <QString>
#include <QTemporaryDir>
#include <QtEnvironmentVariables>
#include <QtSystemDetection>

#include <gtest/gtest.h>

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using hio::ErrorCode;
using hio::diag::CheckResult;
using hio::diag::CheckRunner;
using hio::diag::CheckStatus;
using hio::diag::IProcessSource;

class ListSource final : public IProcessSource {
public:
    explicit ListSource(std::vector<Process> processes) : m_processes(std::move(processes)) {}
    hio::Result<std::vector<Process>> list(std::size_t /*maxCount*/,
                                           const std::stop_token& /*stop*/) override {
        return m_processes;
    }

private:
    std::vector<Process> m_processes;
};

constexpr const char* oneProbe = R"({"libraryVersion": 81000, "probes": [
    {"serialNumber": 801000001, "product": "Test Probe",
     "firmware": "J-Link V1 compiled Oct  1 2024 10:00:00"}]})";

class Environment : public ::testing::Test {
protected:
    void SetUp() override { scenario(oneProbe); }
    void TearDown() override {
        qunsetenv("HIO_FAKE_JLINK_SCENARIO");
        qunsetenv("HIO_FAKE_NRFUTIL_MODE");
    }

    void scenario(const char* json) const {
        const QString path = m_dir.filePath(QStringLiteral("scenario.json"));
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(json);
        file.close();
        qputenv("HIO_FAKE_JLINK_SCENARIO", path.toUtf8());
    }

    [[nodiscard]] fs::path root() const { return {m_dir.path().toStdString()}; }

    [[nodiscard]] hio::diag::EnvironmentContext context() const {
        hio::diag::EnvironmentContext context;
        context.jlink.option = fs::path(HIO_FAKE_JLINK_LIBRARY);
        context.nrfutil.option = fs::path(HIO_FAKE_NRFUTIL);
        context.settingsDir = root() / "settings";
        context.logDir = root() / "logs";
        context.processes = std::make_shared<ListSource>(std::vector<IProcessSource::Process>{});
        context.instanceRoot = root() / "instances";
        context.lockDirectory = root() / "locks";
        context.usbSysfsRoot = root() / "sys"; // empty: no SEGGER device
        context.usbDevRoot = root() / "dev";
        context.facts = std::make_shared<hio::diag::EnvironmentFacts>();
        return context;
    }

    [[nodiscard]] static std::vector<CheckResult>
    run(const hio::diag::EnvironmentContext& context) {
        return CheckRunner::run(hio::diag::environmentChecks(context), {});
    }

    [[nodiscard]] static const CheckResult& find(const std::vector<CheckResult>& results,
                                                 const std::string& id) {
        for (const CheckResult& result : results) {
            if (result.id == id) {
                return result;
            }
        }
        static const CheckResult none;
        ADD_FAILURE() << "no result " << id;
        return none;
    }

private:
    QTemporaryDir m_dir;
};

TEST_F(Environment, AllGood) {
    fs::create_directories(root() / "sys/bus/usb/devices");
    const auto context = this->context();
    const auto results = run(context);
    ASSERT_EQ(results.size(), 7U);
    std::vector<std::string> ids;
    ids.reserve(results.size());
    for (const auto& result : results) {
        ids.push_back(result.id);
    }
    EXPECT_EQ(ids, hio::diag::environmentCheckIds());
    EXPECT_EQ(find(results, "jlink-software").status, CheckStatus::Ok);
    EXPECT_TRUE(find(results, "jlink-software").result.starts_with("8.10 · "));
    EXPECT_EQ(find(results, "nrfutil").status, CheckStatus::Ok);
    EXPECT_EQ(find(results, "nrfutil").result, "7.13.0, device 2.8.1");
#ifdef Q_OS_LINUX
    EXPECT_EQ(find(results, "usb-access").status, CheckStatus::Skipped);
    EXPECT_EQ(find(results, "usb-access").result, "No J-Link connected");
#endif
    EXPECT_EQ(find(results, "probe-holders").status, CheckStatus::Ok);
    EXPECT_EQ(find(results, "probe-holders").result, "None found");
    EXPECT_EQ(find(results, "probes").status, CheckStatus::Ok);
    EXPECT_EQ(find(results, "probes").result, "1 found: 801000001");
    EXPECT_EQ(find(results, "probe-firmware").status, CheckStatus::Ok);
    EXPECT_EQ(find(results, "app-dirs").status, CheckStatus::Ok);
    ASSERT_EQ(context.facts->probes.size(), 1U);
    EXPECT_EQ(context.facts->jlinkVersion, 81000U);
    EXPECT_EQ(context.facts->nrfUtilVersion, "7.13.0");
    // The library copies are unloaded and their instance directories removed.
    std::size_t left = 0;
    if (fs::exists(root() / "instances")) {
        for ([[maybe_unused]] const auto& entry : fs::directory_iterator(root() / "instances")) {
            ++left;
        }
    }
    EXPECT_EQ(left, 0U);
}

TEST_F(Environment, MissingJLinkFailsAndSkipsTheProbeChecks) {
    auto context = this->context();
    context.jlink.option = root() / "no-such-library.so";
    const auto results = run(context);
    const auto& jlink = find(results, "jlink-software");
    EXPECT_EQ(jlink.status, CheckStatus::Failed);
    ASSERT_TRUE(jlink.error.has_value());
    EXPECT_EQ(jlink.error.value_or(hio::Error{}).code, ErrorCode::DependencyJLinkNotFound);
    EXPECT_EQ(find(results, "probes").status, CheckStatus::Skipped);
    EXPECT_EQ(find(results, "probe-firmware").status, CheckStatus::Skipped);
}

TEST_F(Environment, TooOldJLinkFails) {
    scenario(R"({"libraryVersion": 79000, "probes": []})");
    const auto results = run(context());
    const auto& jlink = find(results, "jlink-software");
    EXPECT_EQ(jlink.status, CheckStatus::Failed);
    ASSERT_TRUE(jlink.error.has_value());
    EXPECT_EQ(jlink.error.value_or(hio::Error{}).code, ErrorCode::DependencyJLinkTooOld);
}

TEST_F(Environment, LoadFailureFails) {
    auto context = this->context();
    const fs::path bogus = root() / "libjlinkarm.so";
    {
        QFile file(QString::fromStdString(bogus.string()));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("not a library");
    }
    context.jlink.option = bogus;
    const auto results = run(context);
    const auto& jlink = find(results, "jlink-software");
    EXPECT_EQ(jlink.status, CheckStatus::Failed);
    ASSERT_TRUE(jlink.error.has_value());
    EXPECT_EQ(jlink.error.value_or(hio::Error{}).code, ErrorCode::DependencyJLinkLoadFailed);
}

TEST_F(Environment, NoProbeIsAWarning) {
    scenario(R"({"libraryVersion": 81000, "probes": []})");
    const auto results = run(context());
    const auto& probes = find(results, "probes");
    EXPECT_EQ(probes.status, CheckStatus::Warning);
    ASSERT_TRUE(probes.error.has_value());
    EXPECT_EQ(probes.error.value_or(hio::Error{}).code, ErrorCode::ProbeNotFound);
    EXPECT_EQ(find(results, "probe-firmware").status, CheckStatus::Skipped);
}

TEST_F(Environment, OutdatedFirmwareIsAWarning) {
    scenario(R"({"libraryVersion": 81000, "probes": [
        {"serialNumber": 801000001, "product": "Test Probe",
         "firmware": "J-Link V1 compiled Oct  1 2024 10:00:00",
         "libraryFirmware": "J-Link V1 compiled Oct  1 2025 10:00:00"}]})");
    const auto results = run(context());
    const auto& firmware = find(results, "probe-firmware");
    EXPECT_EQ(firmware.status, CheckStatus::Warning);
    ASSERT_TRUE(firmware.error.has_value());
    EXPECT_EQ(firmware.error.value_or(hio::Error{}).code, ErrorCode::ProbeFirmwareOutdated);
}

// Review V8: a probe that nothing holds but that cannot be opened is not "in use": the check says
// it could not be checked, with the error.
TEST_F(Environment, ProbeThatCannotBeOpenedIsNotInUse) {
    scenario(R"({"libraryVersion": 81000, "probes": [
        {"serialNumber": 801000001, "product": "Test Probe",
         "firmware": "J-Link V1 compiled Oct  1 2024 10:00:00",
         "openError": "Cannot connect to J-Link."}]})");
    const auto results = run(context());
    EXPECT_EQ(find(results, "probes").status, CheckStatus::Ok);
    const auto& firmware = find(results, "probe-firmware");
    EXPECT_EQ(firmware.status, CheckStatus::Warning);
    ASSERT_TRUE(firmware.error.has_value());
    const hio::Error error = firmware.error.value_or(hio::Error{});
    EXPECT_NE(error.code, ErrorCode::ProbeInUseByOtherProcess);
    EXPECT_TRUE(firmware.result.starts_with("Could not be checked: ")) << firmware.result;
    EXPECT_NE(firmware.result.find(error.message), std::string::npos);
    EXPECT_NE(error.detail.find("801000001"), std::string::npos) << error.detail;
}

TEST_F(Environment, LockedProbeIsNotOpened) {
    const auto context = this->context();
    auto lock = hio::device::ProbeLock::acquire(801000001, context.lockDirectory);
    ASSERT_TRUE(lock.has_value());
    const auto results = run(context);
    EXPECT_EQ(find(results, "probes").status, CheckStatus::Ok);
    EXPECT_EQ(find(results, "probe-firmware").status, CheckStatus::Skipped);
    ASSERT_EQ(context.facts->probes.size(), 1U);
    EXPECT_FALSE(context.facts->probes.front().inUseBy.empty());
}

TEST_F(Environment, NrfUtilMissingTooOldOrWithoutDeviceIsAWarning) {
    auto context = this->context();
    context.nrfutil.option = root() / "no-nrfutil";
    auto results = run(context);
    EXPECT_EQ(find(results, "nrfutil").status, CheckStatus::Warning);
    EXPECT_EQ(find(results, "nrfutil").error.value_or(hio::Error{}).code,
              ErrorCode::DependencyNrfUtilNotFound);
    EXPECT_EQ(find(results, "nrfutil").result, "Not found — needed only for LTE modem firmware");

    context.nrfutil.option = fs::path(HIO_FAKE_NRFUTIL);
    qputenv("HIO_FAKE_NRFUTIL_MODE", "old");
    results = run(context);
    EXPECT_EQ(find(results, "nrfutil").status, CheckStatus::Warning);
    EXPECT_EQ(find(results, "nrfutil").error.value_or(hio::Error{}).code,
              ErrorCode::DependencyNrfUtilTooOld);

    qputenv("HIO_FAKE_NRFUTIL_MODE", "nodevice");
    results = run(context);
    EXPECT_EQ(find(results, "nrfutil").status, CheckStatus::Warning);
    EXPECT_EQ(find(results, "nrfutil").error.value_or(hio::Error{}).code,
              ErrorCode::DependencyNrfUtilDeviceMissing);
}

TEST_F(Environment, ProbeHolderIsAWarning) {
    auto context = this->context();
    context.processes = std::make_shared<ListSource>(std::vector<IProcessSource::Process>{
        {.pid = 4242, .name = "JLinkExe", .executablePath = ""}});
    const auto results = run(context);
    const auto& holders = find(results, "probe-holders");
    EXPECT_EQ(holders.status, CheckStatus::Warning);
    EXPECT_EQ(holders.result, "J-Link Commander (PID 4242) is running");
    ASSERT_TRUE(holders.error.has_value());
    EXPECT_EQ(holders.error.value_or(hio::Error{}).code, ErrorCode::ProbeHeldByProgram);
    EXPECT_EQ(holders.error.value_or(hio::Error{}).message,
              "J-Link Commander (PID 4242) is running and may be using the J-Link.");
    EXPECT_EQ(holders.error.value_or(hio::Error{}).suggestion,
              "Close J-Link Commander and try again.");
}

#ifdef Q_OS_LINUX
TEST_F(Environment, UnwritableFolderFails) {
    if (::geteuid() == 0) {
        GTEST_SKIP() << "root can write everywhere";
    }
    auto context = this->context();
    fs::create_directories(root() / "ro");
    fs::permissions(root() / "ro", fs::perms::owner_read | fs::perms::owner_exec);
    context.logDir = root() / "ro";
    const auto results = run(context);
    const auto& dirs = find(results, "app-dirs");
    EXPECT_EQ(dirs.status, CheckStatus::Failed);
    ASSERT_TRUE(dirs.error.has_value());
    EXPECT_EQ(dirs.error.value_or(hio::Error{}).code, ErrorCode::IoWriteFailed);
    fs::permissions(root() / "ro", fs::perms::owner_all);
}
#endif

} // namespace
