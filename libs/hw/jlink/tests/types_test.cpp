#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/types.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

using hio::jlink::FirmwareStatus;

TEST(LibraryVersion, Format) {
    EXPECT_EQ(hio::jlink::formatLibraryVersion(81000), "8.10");
    EXPECT_EQ(hio::jlink::formatLibraryVersion(81001), "8.10a");
    EXPECT_EQ(hio::jlink::formatLibraryVersion(79411), "7.94k");
    EXPECT_EQ(hio::jlink::formatLibraryVersion(80000), "8.00");
    EXPECT_EQ(hio::jlink::formatLibraryVersion(80299), "8.02.99");
}

TEST(LibraryVersion, MinimumIs800) { EXPECT_EQ(hio::jlink::minimumLibraryVersion, 80000U); }

TEST(LibraryVersion, FromPath) {
    using hio::jlink::versionFromPath;
    EXPECT_EQ(versionFromPath("/opt/SEGGER/JLink/libjlinkarm.so.8.10.1"), 81001U);
    EXPECT_EQ(versionFromPath("/opt/SEGGER/JLink_V794k/libjlinkarm.so"), 79411U);
    EXPECT_EQ(versionFromPath("/opt/SEGGER/JLink_V810/libjlinkarm.so.8"), 81000U);
    EXPECT_EQ(versionFromPath("/Applications/SEGGER/JLink/libjlinkarm.8.10.2.dylib"), 81002U);
    EXPECT_EQ(versionFromPath("/opt/SEGGER/JLink/libjlinkarm.so"), 0U);
    EXPECT_EQ(versionFromPath("C:/Program Files/SEGGER/JLink_V812c/JLink_x64.dll"), 81203U);
    EXPECT_EQ(versionFromPath("/x/libjlinkarm.so.99999999999999999999.1.1"), 0U);
}

TEST(Firmware, IdIsThePartBeforeCompiled) {
    EXPECT_EQ(hio::jlink::firmwareId("J-Link V11 compiled Sep 21 2023 10:38:43"), "J-Link V11 ");
    EXPECT_EQ(hio::jlink::firmwareId("no marker"), "no marker");
}

TEST(Firmware, CompareLikePylink) {
    const auto status = [](const char* probe, const char* library) {
        return hio::jlink::compareFirmware(probe, library);
    };
    EXPECT_EQ(status("J-Link V11 compiled Sep 21 2023 10:38:43",
                     "J-Link V11 compiled Sep 21 2023 10:38:43"),
              FirmwareStatus::Current);
    EXPECT_EQ(status("J-Link V11 compiled Sep 21 2023 10:38:43",
                     "J-Link V11 compiled Oct  1 2024 09:00:00"),
              FirmwareStatus::Outdated);
    EXPECT_EQ(status("J-Link V11 compiled Oct  1 2024 09:00:00",
                     "J-Link V11 compiled Sep 21 2023 10:38:43"),
              FirmwareStatus::Newer);
    // Same date, different string: pylink's firmware_newer() says newer.
    EXPECT_EQ(status("J-Link V11 compiled Sep 21 2023 10:38:43 (beta)",
                     "J-Link V11 compiled Sep 21 2023 10:38:43"),
              FirmwareStatus::Newer);
    EXPECT_FALSE(status("J-Link V11", "J-Link V11 compiled Sep 21 2023 10:38:43").has_value());
    EXPECT_FALSE(status("J-Link V11 compiled Foo 21 2023 10:38:43",
                        "J-Link V11 compiled Sep 21 2023 10:38:43")
                     .has_value());
}

TEST(Ids, StableJsonIds) {
    EXPECT_EQ(hio::jlink::connectionId(hio::jlink::Connection::Usb), "usb");
    EXPECT_EQ(hio::jlink::connectionId(hio::jlink::Connection::Ip), "ip");
    EXPECT_EQ(hio::jlink::firmwareStatusId(FirmwareStatus::Current), "current");
    EXPECT_EQ(hio::jlink::firmwareStatusId(FirmwareStatus::Outdated), "outdated");
    EXPECT_EQ(hio::jlink::firmwareStatusId(FirmwareStatus::Newer), "newer");
}

} // namespace
