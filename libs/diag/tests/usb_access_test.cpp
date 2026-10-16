// checkUsbAccess on a temporary sysfs / dev tree (plan/features/0007, A3; ADR-0072).

#include "hio/core/error.h"
#include "hio/diag/usb_access.h"

#include <QTemporaryDir>
#include <QtSystemDetection>

#include <gtest/gtest.h>

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

#include <filesystem>
#include <format>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using hio::ErrorCode;

class UsbAccess : public ::testing::Test {
protected:
    [[nodiscard]] fs::path root() const { return {m_dir.path().toStdString()}; }

    void addDevice(const std::string& entry, const std::string& vendor, int bus, int device,
                   bool accessible) const {
        const fs::path dir = root() / "sys/bus/usb/devices" / entry;
        fs::create_directories(dir);
        std::ofstream(dir / "idVendor") << vendor << "\n";
        std::ofstream(dir / "busnum") << bus << "\n";
        std::ofstream(dir / "devnum") << device << "\n";
        std::ofstream(dir / "product") << "Test Probe\n";
        const fs::path node =
            root() / "dev/bus/usb" / std::format("{:03}", bus) / std::format("{:03}", device);
        fs::create_directories(node.parent_path());
        std::ofstream(node) << "";
        fs::permissions(node, accessible ? fs::perms::owner_read | fs::perms::owner_write
                                         : fs::perms::none);
    }

private:
    QTemporaryDir m_dir;
};

#ifdef Q_OS_LINUX

TEST_F(UsbAccess, NoSeggerDeviceGivesAnEmptyList) {
    addDevice("1-1", "046d", 1, 2, true);
    fs::create_directories(root() / "sys/bus/usb/devices/1-1:1.0"); // an interface entry
    const auto devices = hio::diag::checkUsbAccess(root() / "sys", root() / "dev");
    ASSERT_TRUE(devices.has_value()) << devices.error().detail;
    EXPECT_TRUE(devices->empty());
}

TEST_F(UsbAccess, AccessibleProbe) {
    addDevice("1-2", "1366", 1, 5, true);
    const auto devices = hio::diag::checkUsbAccess(root() / "sys", root() / "dev");
    ASSERT_TRUE(devices.has_value());
    ASSERT_EQ(devices->size(), 1U);
    EXPECT_EQ(devices->front().bus, 1);
    EXPECT_EQ(devices->front().device, 5);
    EXPECT_EQ(devices->front().product, "Test Probe");
    EXPECT_TRUE(devices->front().accessible);
    EXPECT_FALSE(hio::diag::usbAccessDeniedError(*devices).has_value());
}

TEST_F(UsbAccess, InaccessibleProbeIsUsbAccessDenied) {
    if (::geteuid() == 0) {
        GTEST_SKIP() << "root can access every file";
    }
    addDevice("3-1", "1366", 3, 12, false);
    addDevice("1-2", "1366", 1, 5, true);
    const auto devices = hio::diag::checkUsbAccess(root() / "sys", root() / "dev");
    ASSERT_TRUE(devices.has_value());
    ASSERT_EQ(devices->size(), 2U);
    EXPECT_EQ(devices->front().bus, 1); // sorted
    EXPECT_FALSE(devices->back().accessible);
    const auto error = hio::diag::usbAccessDeniedError(*devices);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error.value_or(hio::Error{}).code, ErrorCode::UsbAccessDenied);
    EXPECT_NE(error.value_or(hio::Error{}).detail.find("/dev/bus/usb/003/012"), std::string::npos);
}

TEST_F(UsbAccess, MissingSysfsIsReadError) {
    const auto devices = hio::diag::checkUsbAccess(root() / "nothing", root() / "dev");
    ASSERT_FALSE(devices.has_value());
    EXPECT_EQ(devices.error().code, ErrorCode::IoReadFailed);
}

#endif

TEST(UsbRule, CommandsContainTheRule) {
    const auto commands = hio::diag::jlinkUdevCommands();
    ASSERT_EQ(commands.size(), 2U);
    EXPECT_NE(commands[0].find(hio::diag::jlinkUdevRule), std::string::npos);
    EXPECT_NE(commands[0].find("/etc/udev/rules.d/99-jlink.rules"), std::string::npos);
    EXPECT_EQ(commands[1], "sudo udevadm control --reload-rules && sudo udevadm trigger");
}

} // namespace
