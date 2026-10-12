// ProbePickerViewModel with the ProbeScanner worker and the fake J-Link library: rows, firmware
// date, preselection, in use by another tab and by another process (ProbeLock), missing library
// (plan 0004, B3; ADR-0050, ADR-0058).

#include "gui_test_support.h"
#include "lock_holder.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/probe_picker_view_model.h"
#include "workers/probe_scanner.h"

#include <QModelIndex>
#include <QSignalSpy>
#include <QString>
#include <QtEnvironmentVariables>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>

namespace {

using hio::gui::AppViewModel;
using hio::gui::DeviceTabViewModel;
using hio::gui::ProbePickerViewModel;
using hio::gui::test::spin;
using hio::gui::test::waitFor;

class ProbePickerTest : public hio::gui::test::GuiTest {
protected:
    static QVariant role(const ProbePickerViewModel& picker, int row, int role) {
        return picker.data(picker.index(row), role);
    }
    static bool waitForRows(ProbePickerViewModel& picker, int rows) {
        return waitFor([&] { return picker.scanned() && picker.count() == rows; });
    }
};

TEST_F(ProbePickerTest, FirmwareDate) {
    EXPECT_EQ(ProbePickerViewModel::firmwareDate(
                  QStringLiteral("J-Link V11 compiled Sep 21 2023 10:38:43")),
              QStringLiteral("2023-09-21"));
    EXPECT_EQ(ProbePickerViewModel::firmwareDate(QStringLiteral("J-Link compiled Mar  7 2024")),
              QStringLiteral("2024-03-07"));
    EXPECT_TRUE(ProbePickerViewModel::firmwareDate(QStringLiteral("garbage")).isEmpty());
    EXPECT_TRUE(ProbePickerViewModel::firmwareDate(QString()).isEmpty());
}

TEST_F(ProbePickerTest, ListsProbesOnlyWhileActive) {
    const auto app = makeApp();
    ProbePickerViewModel& picker = *app->currentTabViewModel()->picker();
    spin(std::chrono::milliseconds(500));
    EXPECT_FALSE(picker.scanned()); // no picker visible: no scan
    picker.setActive(true);
    EXPECT_EQ(app->probeScanner().activeCount(), 1);
    ASSERT_TRUE(waitForRows(picker, 2));
    EXPECT_EQ(role(picker, 0, ProbePickerViewModel::SerialRole).toString(),
              QStringLiteral("801000001"));
    EXPECT_EQ(role(picker, 0, ProbePickerViewModel::ProductRole).toString(),
              QStringLiteral("Test Probe"));
    EXPECT_EQ(role(picker, 0, ProbePickerViewModel::FirmwareRole).toString(),
              QStringLiteral("2023-09-21"));
    EXPECT_EQ(role(picker, 0, ProbePickerViewModel::InUseTabRole).toInt(), -1);
    EXPECT_TRUE(role(picker, 0, ProbePickerViewModel::InUseOtherRole).toString().isEmpty());
    EXPECT_EQ(picker.libraryVersion(), QStringLiteral("8.10"));
    EXPECT_FALSE(picker.libraryError().valid());
    EXPECT_EQ(picker.selectedRow(), -1); // two free probes: nothing preselected
    EXPECT_FALSE(picker.canConnect());
    picker.setActive(false);
    EXPECT_EQ(app->probeScanner().activeCount(), 0);
}

TEST_F(ProbePickerTest, OneFreeProbeIsPreselectedAndConnects) {
    useScenario(R"({"probes": [{"serialNumber": 801000007, "product": "Solo"}]})");
    const auto app = makeApp();
    DeviceTabViewModel& tab = *app->currentTabViewModel();
    ProbePickerViewModel& picker = *tab.picker();
    picker.setActive(true);
    ASSERT_TRUE(waitForRows(picker, 1));
    EXPECT_EQ(picker.selectedRow(), 0);
    ASSERT_TRUE(picker.canConnect());
    picker.setTargetId(QStringLiteral("app"));
    picker.connectSelected();
    EXPECT_EQ(tab.probeSerial(), 801000007U);
    EXPECT_EQ(tab.product(), QStringLiteral("Solo"));
    EXPECT_FALSE(picker.active()); // the picker is hidden now
    EXPECT_NE(tab.state(), DeviceTabViewModel::ChoosingDevice);
    ASSERT_TRUE(waitFor([&] { return tab.state() == DeviceTabViewModel::Connected; }));
}

TEST_F(ProbePickerTest, ProbeUsedByAnotherTab) {
    const auto app = makeApp();
    ASSERT_TRUE(app->currentTabViewModel()->configure(QStringLiteral("chester"),
                                                      QStringLiteral("app"), 801000002));
    app->newTab();
    ProbePickerViewModel& picker = *app->currentTabViewModel()->picker();
    QSignalSpy activate(&picker, &ProbePickerViewModel::tabActivationRequested);
    picker.setActive(true);
    ASSERT_TRUE(waitForRows(picker, 2));
    EXPECT_EQ(role(picker, 1, ProbePickerViewModel::InUseTabRole).toInt(), 1); // "tab 1"
    // The only free probe is preselected.
    EXPECT_EQ(picker.selectedRow(), 0);
    picker.choose(1);
    ASSERT_EQ(activate.count(), 1);
    EXPECT_EQ(activate.at(0).at(0).toInt(), 0);
    EXPECT_EQ(app->currentTab(), 0); // AppViewModel switched to that tab
}

TEST_F(ProbePickerTest, ProbeUsedByAnotherProcess) {
    const auto lockDir = root() / "locks";
    std::filesystem::create_directories(lockDir);
    hio::device::test::LockHolder holder(
        QString::fromStdU16String((lockDir / "probe-801000001.lock").u16string()));
    ASSERT_TRUE(holder.waitUntilLocked());

    const auto app = makeApp();
    ProbePickerViewModel& picker = *app->currentTabViewModel()->picker();
    picker.setActive(true);
    ASSERT_TRUE(waitForRows(picker, 2));
    const QString other = role(picker, 0, ProbePickerViewModel::InUseOtherRole).toString();
    EXPECT_TRUE(other.contains(QStringLiteral("PID %1").arg(holder.pid()))) << other.toStdString();
    EXPECT_FALSE(role(picker, 0, ProbePickerViewModel::SelectableRole).toBool());
    picker.setSelectedRow(0);           // disabled row
    EXPECT_EQ(picker.selectedRow(), 1); // the other one was preselected as the only free probe
    holder.release();
    picker.refresh();
    ASSERT_TRUE(waitFor([&] {
        return role(picker, 0, ProbePickerViewModel::InUseOtherRole).toString().isEmpty();
    }));
}

TEST_F(ProbePickerTest, MissingLibrary) {
    qputenv("HIO_JLINK_LIBRARY", "/nonexistent/libjlinkarm.so");
    const auto app = makeApp();
    ProbePickerViewModel& picker = *app->currentTabViewModel()->picker();
    picker.setActive(true);
    ASSERT_TRUE(waitFor([&] { return picker.scanned(); }));
    EXPECT_EQ(picker.count(), 0);
    ASSERT_TRUE(picker.libraryError().valid());
    EXPECT_EQ(picker.libraryError().code(), QStringLiteral("dependency.jlink_not_found"));
}

TEST_F(ProbePickerTest, NoProbes) {
    useScenario(R"({"probes": []})");
    const auto app = makeApp();
    ProbePickerViewModel& picker = *app->currentTabViewModel()->picker();
    picker.setActive(true);
    ASSERT_TRUE(waitFor([&] { return picker.scanned(); }));
    EXPECT_EQ(picker.count(), 0);
    EXPECT_FALSE(picker.libraryError().valid());
    EXPECT_EQ(picker.libraryVersion(), QStringLiteral("8.10"));
}

TEST_F(ProbePickerTest, ProfilesAndTargets) {
    const auto app = makeApp();
    ProbePickerViewModel& picker = *app->currentTabViewModel()->picker();
    EXPECT_EQ(picker.profileId(), QStringLiteral("chester"));
    ASSERT_EQ(ProbePickerViewModel::profiles().size(), 1);
    ASSERT_EQ(picker.targets().size(), 2);
    EXPECT_EQ(picker.targetId(), QStringLiteral("app"));
    picker.setTargetId(QStringLiteral("lte"));
    EXPECT_EQ(picker.targetId(), QStringLiteral("lte"));
    picker.setTargetId(QStringLiteral("nope"));
    EXPECT_EQ(picker.targetId(), QStringLiteral("lte"));
    EXPECT_EQ(ProbePickerViewModel::targetTitle(QStringLiteral("app")),
              QStringLiteral("Application MCU (app)"));
}

} // namespace
