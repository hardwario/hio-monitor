// The probe-holder process scan with a fake process source (plan/features/0007, A4; ADR-0073).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/diag/process_scan.h"

#include <QCoreApplication>
#include <QtTypes>

#include <gtest/gtest.h>

#include <cstddef>
#include <expected>
#include <stop_token>
#include <utility>
#include <vector>

namespace {

using hio::ErrorCode;
using hio::diag::IProcessSource;

class FakeSource final : public IProcessSource {
public:
    explicit FakeSource(std::vector<Process> processes) : m_processes(std::move(processes)) {}
    hio::Result<std::vector<Process>> list(std::size_t maxCount,
                                           const std::stop_token& stop) override {
        m_requested = maxCount;
        if (stop.stop_requested()) {
            return std::unexpected(hio::makeError(ErrorCode::Timeout));
        }
        if (m_processes.size() > maxCount) {
            m_processes.resize(maxCount);
        }
        return m_processes;
    }
    [[nodiscard]] std::size_t requested() const noexcept { return m_requested; }

private:
    std::vector<Process> m_processes;
    std::size_t m_requested = 0;
};

TEST(ProcessScan, KnownHolderTable) {
    struct Row {
        const char* name;
        const char* display;
    };
    for (const Row& row : {
             Row{.name = "JLinkExe", .display = "J-Link Commander"},
             Row{.name = "JLink.exe", .display = "J-Link Commander"},
             Row{.name = "jlinkexe", .display = "J-Link Commander"},
             Row{.name = "JLinkGDBServerCLExe", .display = "J-Link GDB Server"},
             Row{.name = "JLinkGDBServerC",
                 .display = "J-Link GDB Server"}, // Linux comm is cut at 15 characters
             Row{.name = "JLinkRTTViewerExe", .display = "J-Link RTT Viewer"},
             Row{.name = "JLinkRTTLogger", .display = "J-Link RTT Logger"},
             Row{.name = "JFlashExe", .display = "J-Flash"},
             Row{.name = "JFlashLite.exe", .display = "J-Flash"},
             Row{.name = "Ozone", .display = "Ozone"},
             Row{.name = "nrfutil", .display = "nRF Util"},
             Row{.name = "nrfutil-device", .display = "nRF Util"},
             Row{.name = "nrfjprog.exe", .display = "nrfjprog"},
             Row{.name = "nRF Connect for Desktop", .display = "nRF Connect for Desktop"},
             Row{.name = "nrfconnect", .display = "nRF Connect for Desktop"},
             Row{.name = "hiomon", .display = "HARDWARIO Monitor (version 1)"},
             Row{.name = "HARDWARIO Monitor.exe", .display = "HARDWARIO Monitor (version 1)"},
             Row{.name = "pyocd", .display = "pyOCD"},
             Row{.name = "openocd", .display = "OpenOCD"},
         }) {
        EXPECT_EQ(hio::diag::knownHolderName(row.name), row.display) << row.name;
    }
    for (const char* other : {"bash", "hio", "hio-monitor", "hio.exe", "HIO-MONITOR.exe", "jlinkx",
                              "openocd-wrapper", "python3", ""}) {
        EXPECT_EQ(hio::diag::knownHolderName(other), "") << other;
    }
}

TEST(ProcessScan, FindsHoldersAndExcludesOurOwnProcesses) {
    const qint64 self = QCoreApplication::applicationPid();
    FakeSource source({
        {.pid = 4242, .name = "JLinkExe", .executablePath = "/opt/SEGGER/JLink/JLinkExe"},
        {.pid = 7, .name = "bash", .executablePath = "/usr/bin/bash"},
        {.pid = 99, .name = "hio", .executablePath = "/usr/bin/hio"},
        {.pid = 100, .name = "hio-monitor", .executablePath = ""},
        {.pid = self, .name = "JLinkExe", .executablePath = ""},
        {.pid = 300, .name = "JLinkGDBServerC", .executablePath = "/opt/x/JLinkGDBServerCLExe"},
        {.pid = 5, .name = "python3", .executablePath = "/usr/bin/nrfutil"},
    });
    const auto holders = hio::diag::findProbeHolders(source, {});
    ASSERT_TRUE(holders.has_value());
    EXPECT_EQ(source.requested(), hio::diag::maxInspectedProcesses);
    ASSERT_EQ(holders->size(), 3U);
    EXPECT_EQ((*holders)[0].pid, 5);
    EXPECT_EQ((*holders)[0].displayName, "nRF Util");
    EXPECT_EQ((*holders)[1].pid, 300);
    EXPECT_EQ((*holders)[1].displayName, "J-Link GDB Server");
    EXPECT_EQ((*holders)[1].executable, "JLinkGDBServerCLExe");
    EXPECT_EQ((*holders)[2].pid, 4242);
    EXPECT_EQ((*holders)[2].displayName, "J-Link Commander");
}

TEST(ProcessScan, BoundOfInspectedProcesses) {
    std::vector<IProcessSource::Process> many;
    for (qint64 pid = 1; pid <= 5000; ++pid) {
        many.push_back({.pid = pid + 100000, .name = "JLinkExe", .executablePath = ""});
    }
    FakeSource source(std::move(many));
    const auto holders = hio::diag::findProbeHolders(source, {});
    ASSERT_TRUE(holders.has_value());
    EXPECT_EQ(holders->size(), hio::diag::maxInspectedProcesses);
}

TEST(ProcessScan, StopRequestIsTimeout) {
    FakeSource source({});
    std::stop_source stop;
    stop.request_stop();
    const auto holders = hio::diag::findProbeHolders(source, stop.get_token());
    ASSERT_FALSE(holders.has_value());
    EXPECT_EQ(holders.error().code, ErrorCode::Timeout);
}

TEST(ProcessScan, PlatformSourceListsThisProcess) {
    auto source = hio::diag::platformProcessSource();
    ASSERT_NE(source, nullptr);
    const auto processes = source->list(hio::diag::maxInspectedProcesses, {});
    ASSERT_TRUE(processes.has_value()) << processes.error().detail;
    const qint64 self = QCoreApplication::applicationPid();
    bool found = false;
    for (const auto& process : *processes) {
        found = found || process.pid == self;
    }
    EXPECT_TRUE(found || processes->size() == hio::diag::maxInspectedProcesses);
}

} // namespace
