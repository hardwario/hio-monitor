// checkUsbAccess on a temporary sysfs / dev tree (plan/features/0007, A3; ADR-0072).

#include "hwio/core/error.h"
#include "hwio/diag/usb_access.h"

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
#include <string_view>

namespace {

namespace fs = std::filesystem;
using hwio::ErrorCode;

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
    const auto devices = hwio::diag::checkUsbAccess(root() / "sys", root() / "dev");
    ASSERT_TRUE(devices.has_value()) << devices.error().detail;
    EXPECT_TRUE(devices->empty());
}

TEST_F(UsbAccess, AccessibleProbe) {
    addDevice("1-2", "1366", 1, 5, true);
    const auto devices = hwio::diag::checkUsbAccess(root() / "sys", root() / "dev");
    ASSERT_TRUE(devices.has_value());
    ASSERT_EQ(devices->size(), 1U);
    EXPECT_EQ(devices->front().bus, 1);
    EXPECT_EQ(devices->front().device, 5);
    EXPECT_EQ(devices->front().product, "Test Probe");
    EXPECT_TRUE(devices->front().accessible);
    EXPECT_FALSE(hwio::diag::usbAccessDeniedError(*devices).has_value());
}

TEST_F(UsbAccess, InaccessibleProbeIsUsbAccessDenied) {
    if (::geteuid() == 0) {
        GTEST_SKIP() << "root can access every file";
    }
    addDevice("3-1", "1366", 3, 12, false);
    addDevice("1-2", "1366", 1, 5, true);
    const auto devices = hwio::diag::checkUsbAccess(root() / "sys", root() / "dev");
    ASSERT_TRUE(devices.has_value());
    ASSERT_EQ(devices->size(), 2U);
    EXPECT_EQ(devices->front().bus, 1); // sorted
    EXPECT_FALSE(devices->back().accessible);
    const auto error = hwio::diag::usbAccessDeniedError(*devices);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error.value_or(hwio::Error{}).code, ErrorCode::UsbAccessDenied);
    EXPECT_NE(error.value_or(hwio::Error{}).detail.find("/dev/bus/usb/003/012"), std::string::npos);
}

// ADR-0185: the uaccess rule needs a local seat; the suggestion says so.
TEST(UsbRule, AccessDeniedSuggestionMentionsTheLocalSeat) {
    const hwio::Error error = hwio::makeError(ErrorCode::UsbAccessDenied);
    EXPECT_NE(error.suggestion.find("users connected only over SSH need their own udev rule"),
              std::string::npos)
        << error.suggestion;
}

TEST_F(UsbAccess, MissingSysfsIsReadError) {
    const auto devices = hwio::diag::checkUsbAccess(root() / "nothing", root() / "dev");
    ASSERT_FALSE(devices.has_value());
    EXPECT_EQ(devices.error().code, ErrorCode::IoReadFailed);
}

#endif

TEST(UsbRule, CommandsContainTheRule) {
    const auto commands = hwio::diag::jlinkUdevCommands();
    ASSERT_EQ(commands.size(), 3U);
    EXPECT_NE(commands[0].find(hwio::diag::jlinkUdevRule), std::string::npos);
    // ADR-0185: before 73-seat-late.rules (uaccess); an old MODE="0666" rule is removed.
    EXPECT_NE(commands[0].find("/etc/udev/rules.d/60-hardwario-jlink.rules"), std::string::npos);
    EXPECT_EQ(commands[1],
              "if grep -qs 'MODE=\"0666\"' /etc/udev/rules.d/99-jlink.rules; then sudo "
              "rm /etc/udev/rules.d/99-jlink.rules; fi");
    EXPECT_EQ(commands[2], "sudo udevadm control --reload-rules && sudo udevadm trigger");
}

// ADR-0185 (review M-3): uaccess only, never a world-writable device node.
TEST(UsbRule, RuleUsesUaccessOnly) {
    EXPECT_EQ(hwio::diag::jlinkUdevRule,
              R"(SUBSYSTEM=="usb", ATTR{idVendor}=="1366", TAG+="uaccess")");
    EXPECT_EQ(std::string_view(hwio::diag::jlinkUdevRule).find("MODE"), std::string_view::npos);
}

// The packaged rule file has the same rule (and no MODE).
TEST(UsbRule, PackagedRuleMatches) {
    std::ifstream file(HWIO_PACKAGED_UDEV_RULE);
    ASSERT_TRUE(file.is_open());
    std::string line;
    std::string rule;
    while (std::getline(file, line)) {
        if (!line.empty() && line.front() != '#') {
            rule = line;
        }
    }
    EXPECT_EQ(rule, hwio::diag::jlinkUdevRule);
}

} // namespace
