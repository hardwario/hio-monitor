// JLinkProbe on a GMock of IJLinkApi and simulated time (plan/features/0002, phase B1).

#include "fake_clock.h"
#include "hio/core/error.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/jlink_probe.h"
#include "hio/hw/jlink/types.h"
#include "mock_jlink_api.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace {

using hio::ErrorCode;
using hio::jlink::FirmwareStatus;
using hio::jlink::HardwareStatus;
using hio::jlink::JLinkProbe;
using hio::jlink::ProbeInfo;
using hio::jlink::test::FakeClock;
using hio::jlink::test::MockJLinkApi;
using ::testing::_;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;

std::vector<ProbeInfo> twoProbes() {
    return {ProbeInfo{.serialNumber = 111,
                      .product = "Test Probe",
                      .connection = hio::jlink::Connection::Usb,
                      .firmware = {}},
            ProbeInfo{.serialNumber = 222,
                      .product = "Test Probe 2",
                      .connection = hio::jlink::Connection::Usb,
                      .firmware = {}}};
}

class Probe : public ::testing::Test {
protected:
    Probe() {
        ON_CALL(m_api, listProbes(_)).WillByDefault(Return(twoProbes()));
        ON_CALL(m_api, open()).WillByDefault(Return(std::nullopt));
        ON_CALL(m_api, firmwareString())
            .WillByDefault(Return(std::string("J-Link V11 compiled Sep 21 2023 10:38:43")));
        ON_CALL(m_api, hardwareStatus())
            .WillByDefault(Return(HardwareStatus{.targetMillivolts = 3300}));
    }

    NiceMock<MockJLinkApi>& api() { return m_api; }
    FakeClock& clock() { return m_clock; }
    JLinkProbe& probe() { return m_probe; }

private:
    NiceMock<MockJLinkApi> m_api;
    FakeClock m_clock;
    JLinkProbe m_probe{m_api, m_clock};
};

TEST_F(Probe, OpensTheFirstProbeWithoutSerialNumber) {
    EXPECT_CALL(api(), selectBySerialNumber(111U)).WillOnce(Return(0));
    EXPECT_CALL(api(), open()).WillOnce(Return(std::nullopt));
    EXPECT_CALL(api(), execCommand(_)).Times(::testing::AnyNumber());
    const auto info = probe().open(std::nullopt);
    ASSERT_TRUE(info) << info.error().detail;
    EXPECT_EQ(info->serialNumber, 111U);
    EXPECT_EQ(info->product, "Test Probe");
    EXPECT_EQ(info->firmware, "J-Link V11 compiled Sep 21 2023 10:38:43");
    EXPECT_TRUE(probe().isOpen());
}

// Review fix R1 (decided 2026-10-10): the automatic firmware update is disabled before and right
// after Open, dialogs are suppressed before Open, and SilentUpdateFW is never sent.
TEST_F(Probe, OpenDisablesTheFirmwareUpdateBeforeAndAfterOpen) {
    using ::testing::StrEq;
    EXPECT_CALL(api(), execCommand(StrEq("SilentUpdateFW"))).Times(0);
    {
        InSequence order;
        EXPECT_CALL(api(), selectBySerialNumber(111U)).WillOnce(Return(0));
        EXPECT_CALL(api(), execCommand(StrEq("DisableAutoUpdateFW")));
        EXPECT_CALL(api(), execCommand(StrEq("SuppressControlPanel")));
        EXPECT_CALL(api(), execCommand(StrEq("SetBatchMode = 1")));
        EXPECT_CALL(api(), execCommand(StrEq("HideDeviceSelection = 1")));
        EXPECT_CALL(api(), execCommand(StrEq("DisableInfoWinFlashDL")));
        EXPECT_CALL(api(), open()).WillOnce(Return(std::nullopt));
        EXPECT_CALL(api(), execCommand(StrEq("DisableAutoUpdateFW")));
        EXPECT_CALL(api(), execCommand(StrEq("SetBatchMode = 1")));
        EXPECT_CALL(api(), execCommand(StrEq("HideDeviceSelection = 1")));
        EXPECT_CALL(api(), execCommand(StrEq("DisableInfoWinFlashDL")));
        EXPECT_CALL(api(), firmwareString())
            .WillOnce(Return(std::string("J-Link V11 compiled Sep 21 2023 10:38:43")));
    }
    EXPECT_CALL(api(), updateFirmwareIfNewer()).Times(0);
    ASSERT_TRUE(probe().open(111U));
}

TEST_F(Probe, OpensBySerialNumber) {
    EXPECT_CALL(api(), selectBySerialNumber(222U)).WillOnce(Return(0));
    const auto info = probe().open(222U);
    ASSERT_TRUE(info);
    EXPECT_EQ(info->product, "Test Probe 2");
}

TEST_F(Probe, UnknownSerialNumberIsNotFound) {
    EXPECT_CALL(api(), open()).Times(0);
    const auto info = probe().open(333U);
    ASSERT_FALSE(info);
    EXPECT_EQ(info.error().code, ErrorCode::ProbeNotFound);
    EXPECT_EQ(info.error().message, "No J-Link probe with serial number 333 found.");
    EXPECT_EQ(info.error().wizard, hio::WizardId::ConnectDevice);
}

TEST_F(Probe, NoProbeIsNotFound) {
    ON_CALL(api(), listProbes(_)).WillByDefault(Return(std::vector<ProbeInfo>{}));
    const auto info = probe().open(std::nullopt);
    ASSERT_FALSE(info);
    EXPECT_EQ(info.error().code, ErrorCode::ProbeNotFound);
    EXPECT_EQ(info.error().message, "No J-Link probe found.");
}

TEST_F(Probe, BusyProbeFailsToOpenWithTheLibraryText) {
    EXPECT_CALL(api(), open()).WillOnce(Return(std::string("Cannot connect to J-Link.")));
    EXPECT_CALL(api(), takeErrorText()).WillOnce(Return(std::string("USB: device busy")));
    const auto info = probe().open(111U);
    ASSERT_FALSE(info);
    EXPECT_EQ(info.error().code, ErrorCode::ProbeOpenFailed);
    EXPECT_NE(info.error().detail.find("Cannot connect to J-Link."), std::string::npos);
    EXPECT_NE(info.error().detail.find("USB: device busy"), std::string::npos);
    EXPECT_FALSE(probe().isOpen());
}

TEST_F(Probe, ListFailureIsOpenFailed) {
    ON_CALL(api(), listProbes(_))
        .WillByDefault(Return(std::expected<std::vector<ProbeInfo>, int>(std::unexpected(-1))));
    const auto info = probe().open(std::nullopt);
    ASSERT_FALSE(info);
    EXPECT_EQ(info.error().code, ErrorCode::ProbeOpenFailed);
}

TEST_F(Probe, ConnectRetriesThreeTimesHalfASecondApart) {
    EXPECT_CALL(api(), execCommand(::testing::StrEq("Device = NRF52840_xxAA"))).Times(1);
    EXPECT_CALL(api(), connect()).WillOnce(Return(-1)).WillOnce(Return(-1)).WillOnce(Return(0));
    const auto status = probe().connect("NRF52840_xxAA", 1000, false, {});
    ASSERT_TRUE(status) << status.error().detail;
    EXPECT_EQ(clock().slept(), std::chrono::milliseconds(1000));
}

TEST_F(Probe, ConnectFailureWithLowVoltageIsNoPower) {
    EXPECT_CALL(api(), connect()).Times(3).WillRepeatedly(Return(-1));
    ON_CALL(api(), hardwareStatus()).WillByDefault(Return(HardwareStatus{.targetMillivolts = 12}));
    const auto status = probe().connect("NRF52840_xxAA", 1000, false, {});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::TargetNoPower);
    EXPECT_EQ(status.error().message, "The device has no power (12 mV).");
    EXPECT_EQ(clock().slept(), std::chrono::milliseconds(1000)); // no wait after the last try
}

// Review R21: a probe that cannot measure VTref reports 0 mV; without a healthy reading on this
// probe a failed connect is target.connect_failed, never target.no_power.
TEST_F(Probe, ConnectFailureWithZeroVoltsBeforeAHealthyReadingIsConnectFailed) {
    EXPECT_CALL(api(), connect()).Times(3).WillRepeatedly(Return(-1));
    ON_CALL(api(), hardwareStatus()).WillByDefault(Return(HardwareStatus{.targetMillivolts = 0}));
    const auto status = probe().connect("NRF52840_xxAA", 1000, false, {});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::TargetConnectFailed);
    EXPECT_NE(status.error().detail.find("VTref 0 mV"), std::string::npos);
}

TEST_F(Probe, ConnectFailureWithZeroVoltsAfterAHealthyReadingIsNoPower) {
    EXPECT_CALL(api(), connect()).Times(3).WillRepeatedly(Return(-1));
    ON_CALL(api(), hardwareStatus()).WillByDefault(Return(HardwareStatus{.targetMillivolts = 0}));
    const auto status = probe().connect("NRF52840_xxAA", 1000, true, {});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::TargetNoPower);
    EXPECT_EQ(status.error().message, "The device has no power (0 mV).");
}

TEST_F(Probe, ConnectFailureWithPowerIsConnectFailed) {
    EXPECT_CALL(api(), connect()).Times(3).WillRepeatedly(Return(-1));
    const auto status = probe().connect("NRF52840_xxAA", 1000, false, {});
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::TargetConnectFailed);
    EXPECT_EQ(status.error().message, "Could not connect to the device's NRF52840_xxAA MCU.");
    EXPECT_NE(status.error().detail.find("3300 mV"), std::string::npos);
}

TEST_F(Probe, ConnectHonoursStop) {
    std::stop_source stop;
    EXPECT_CALL(api(), connect()).WillOnce([&stop] {
        stop.request_stop();
        return -1;
    });
    const auto status = probe().connect("NRF52840_xxAA", 1000, false, stop.get_token());
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::Cancelled);
}

TEST_F(Probe, HardwareStatusFailureMeansTheProbeIsGone) {
    ON_CALL(api(), hardwareStatus()).WillByDefault(Return(std::nullopt));
    const auto status = probe().hardwareStatus();
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::ProbeNotFound);
}

TEST_F(Probe, FirmwareStatus) {
    EXPECT_CALL(api(), embeddedFirmwareString(::testing::StrEq("J-Link V11 ")))
        .WillOnce(Return(std::string("J-Link V11 compiled Oct  1 2024 09:00:00")))
        .WillOnce(Return(std::string("J-Link V11 compiled Sep 21 2023 10:38:43")))
        .WillOnce(Return(std::string("J-Link V11 compiled Jan  1 2020 09:00:00")))
        .WillOnce(Return(std::expected<std::string, int>(std::unexpected(-1))))
        .WillOnce(Return(std::string("garbage")));
    EXPECT_EQ(probe().firmwareStatus(), FirmwareStatus::Outdated);
    EXPECT_EQ(probe().firmwareStatus(), FirmwareStatus::Current);
    EXPECT_EQ(probe().firmwareStatus(), FirmwareStatus::Newer);
    // Review R11: unreadable or uncomparable firmware is Unknown, never Current.
    EXPECT_EQ(probe().firmwareStatus(), FirmwareStatus::Unknown);
    EXPECT_EQ(probe().firmwareStatus(), FirmwareStatus::Unknown);
}

// Review R11: updateFirmware() reopens the probe normally and verifies the status.
class FirmwareUpdate : public Probe {
protected:
    // The probe reports `initial` until updateFirmwareIfNewer() installs the library firmware
    // (when `takesEffect`).
    void script(const std::string& initial, const std::string& library, bool takesEffect) {
        m_firmware = initial;
        ON_CALL(api(), firmwareString()).WillByDefault([this] { return m_firmware; });
        ON_CALL(api(), embeddedFirmwareString(_)).WillByDefault(Return(library));
        ON_CALL(api(), updateFirmwareIfNewer()).WillByDefault([this, library, takesEffect] {
            if (takesEffect) {
                m_firmware = library;
            }
            return std::uint32_t{0x1234};
        });
    }

private:
    std::string m_firmware;
};

constexpr const char* oldFirmware = "J-Link V11 compiled Sep 21 2023 10:38:43";
constexpr const char* libraryFirmware = "J-Link V11 compiled Oct  1 2024 09:00:00";

TEST_F(FirmwareUpdate, OutdatedIsUpdatedAndVerified) {
    script(oldFirmware, libraryFirmware, true);
    EXPECT_CALL(api(), open()).Times(3).WillRepeatedly(Return(std::nullopt));
    EXPECT_CALL(api(), execCommand(_)).Times(::testing::AnyNumber());
    // The first and the verifying (normal) opens disable the automatic update, before and after
    // Open; the update open does not (review R24).
    EXPECT_CALL(api(), execCommand(::testing::StrEq("DisableAutoUpdateFW"))).Times(4);
    EXPECT_CALL(api(), updateFirmwareIfNewer()).Times(1);
    ASSERT_TRUE(probe().open(111U));
    const auto before = probe().updateFirmware();
    ASSERT_TRUE(before) << before.error().detail;
    EXPECT_EQ(*before, FirmwareStatus::Outdated);
    EXPECT_EQ(probe().firmwareStatus(), FirmwareStatus::Current);
}

TEST_F(FirmwareUpdate, StillOutdatedAfterTheUpdateFails) {
    script(oldFirmware, libraryFirmware, false);
    ASSERT_TRUE(probe().open(111U));
    const auto before = probe().updateFirmware();
    ASSERT_FALSE(before);
    EXPECT_EQ(before.error().code, ErrorCode::ProbeFirmwareUpdateFailed);
    EXPECT_EQ(before.error().wizard, hio::WizardId::Troubleshooting);
}

TEST_F(FirmwareUpdate, UnknownIsUpdatedAndVerified) {
    script("unreadable", libraryFirmware, true);
    ASSERT_TRUE(probe().open(111U));
    const auto before = probe().updateFirmware();
    ASSERT_TRUE(before) << before.error().detail;
    EXPECT_EQ(*before, FirmwareStatus::Unknown);
}

TEST_F(FirmwareUpdate, StillUnknownAfterTheUpdateFails) {
    script("unreadable", libraryFirmware, false);
    ASSERT_TRUE(probe().open(111U));
    const auto before = probe().updateFirmware();
    ASSERT_FALSE(before);
    EXPECT_EQ(before.error().code, ErrorCode::ProbeFirmwareUpdateFailed);
}

TEST_F(FirmwareUpdate, NewerIsSynchronizedAfterInvalidating) {
    script(libraryFirmware, oldFirmware, true);
    EXPECT_CALL(api(), execCommand(_)).Times(::testing::AnyNumber());
    {
        InSequence order;
        EXPECT_CALL(api(), execCommand(::testing::StrEq("InvalidateFW"))).Times(1);
        EXPECT_CALL(api(), updateFirmwareIfNewer());
    }
    ASSERT_TRUE(probe().open(111U));
    const auto before = probe().updateFirmware();
    ASSERT_TRUE(before) << before.error().detail;
    EXPECT_EQ(*before, FirmwareStatus::Newer);
}

// Review R24: "up to date" comes only from the Normal open (automatic update disabled); the probe
// is not reopened in FirmwareUpdate mode.
TEST_F(FirmwareUpdate, CurrentFirmwareIsNotUpdated) {
    script(oldFirmware, oldFirmware, true);
    EXPECT_CALL(api(), updateFirmwareIfNewer()).Times(0);
    EXPECT_CALL(api(), open()).Times(1).WillRepeatedly(Return(std::nullopt));
    EXPECT_CALL(api(), execCommand(_)).Times(::testing::AnyNumber());
    EXPECT_CALL(api(), execCommand(::testing::StrEq("DisableAutoUpdateFW"))).Times(2);
    ASSERT_TRUE(probe().open(111U));
    const auto before = probe().updateFirmware();
    ASSERT_TRUE(before);
    EXPECT_EQ(*before, FirmwareStatus::Current);
}

// Review R24: the status "before" is read after the Normal open; only then is the probe reopened
// in FirmwareUpdate mode (no DisableAutoUpdateFW), updated, closed and opened normally again.
TEST_F(FirmwareUpdate, StatusBeforeIsReadAfterANormalOpen) {
    script(oldFirmware, libraryFirmware, true);
    EXPECT_CALL(api(), execCommand(_)).Times(::testing::AnyNumber());
    {
        InSequence order;
        EXPECT_CALL(api(), execCommand(::testing::StrEq("DisableAutoUpdateFW")));
        EXPECT_CALL(api(), open());
        EXPECT_CALL(api(), execCommand(::testing::StrEq("DisableAutoUpdateFW")));
        EXPECT_CALL(api(), embeddedFirmwareString(_)); // status "before"
        EXPECT_CALL(api(), close());
        EXPECT_CALL(api(), open()); // FirmwareUpdate mode
        EXPECT_CALL(api(), updateFirmwareIfNewer());
        EXPECT_CALL(api(), close());
        EXPECT_CALL(api(), execCommand(::testing::StrEq("DisableAutoUpdateFW")));
        EXPECT_CALL(api(), open());
        EXPECT_CALL(api(), execCommand(::testing::StrEq("DisableAutoUpdateFW")));
        EXPECT_CALL(api(), embeddedFirmwareString(_)); // verification
    }
    ASSERT_TRUE(probe().open(111U));
    const auto before = probe().updateFirmware();
    ASSERT_TRUE(before) << before.error().detail;
    EXPECT_EQ(*before, FirmwareStatus::Outdated);
}

// Review R12: sessions use USB probes only; a network J-Link's serial number is not supported.
TEST_F(Probe, ProbesAreListedFromUsbOnly) {
    EXPECT_CALL(api(), listProbes(hio::jlink::ProbeHosts::Usb)).WillOnce(Return(twoProbes()));
    EXPECT_CALL(api(), selectBySerialNumber(111U)).WillOnce(Return(0));
    ASSERT_TRUE(probe().open(std::nullopt));
}

TEST_F(Probe, NetworkProbeSerialNumberIsNotSupported) {
    std::vector<ProbeInfo> all = twoProbes();
    all.insert(all.begin(), ProbeInfo{.serialNumber = 333,
                                      .product = "Test Probe IP",
                                      .connection = hio::jlink::Connection::Ip,
                                      .firmware = {}});
    ON_CALL(api(), listProbes(hio::jlink::ProbeHosts::Usb)).WillByDefault(Return(twoProbes()));
    ON_CALL(api(), listProbes(hio::jlink::ProbeHosts::UsbAndIp)).WillByDefault(Return(all));
    EXPECT_CALL(api(), open()).Times(0);
    const auto info = probe().open(333U);
    ASSERT_FALSE(info);
    EXPECT_EQ(info.error().code, ErrorCode::ProbeNotFound);
    EXPECT_EQ(info.error().message, "The J-Link 333 is a network J-Link; network J-Links are not "
                                    "supported for sessions yet.");
}

// Review R22: the network scan runs on the first failed open of a serial number only.
TEST_F(Probe, NetworkProbeCheckRunsOnlyOnTheFirstFailedOpen) {
    ON_CALL(api(), listProbes(hio::jlink::ProbeHosts::Usb)).WillByDefault(Return(twoProbes()));
    EXPECT_CALL(api(), listProbes(hio::jlink::ProbeHosts::Usb)).Times(3);
    EXPECT_CALL(api(), listProbes(hio::jlink::ProbeHosts::UsbAndIp)).WillOnce(Return(twoProbes()));
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto info = probe().open(444U);
        ASSERT_FALSE(info);
        EXPECT_EQ(info.error().message, "No J-Link probe with serial number 444 found.");
    }
}

TEST_F(Probe, ResetWithoutHaltLetsTheCpuRun) {
    InSequence order;
    EXPECT_CALL(api(), setResetDelay(10));
    EXPECT_CALL(api(), reset()).WillOnce(Return(0));
    EXPECT_CALL(api(), go());
    EXPECT_TRUE(probe().reset(false));
}

TEST_F(Probe, ResetWithHaltDoesNotGo) {
    EXPECT_CALL(api(), reset()).WillOnce(Return(0));
    EXPECT_CALL(api(), go()).Times(0);
    EXPECT_TRUE(probe().reset(true));
}

TEST_F(Probe, ResetFailure) {
    EXPECT_CALL(api(), reset()).WillOnce(Return(-1));
    const auto status = probe().reset(false);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::TargetConnectFailed);
}

TEST_F(Probe, SwdSelection) {
    EXPECT_CALL(api(), selectInterface(hio::jlink::TargetInterface::Swd))
        .WillOnce(Return(0))
        .WillOnce(Return(1));
    EXPECT_TRUE(probe().selectSwd());
    EXPECT_FALSE(probe().selectSwd());
}

TEST_F(Probe, CloseOnlyWhenOpen) {
    EXPECT_CALL(api(), close()).Times(1);
    probe().close(); // not open: no call
    ASSERT_TRUE(probe().open(111U));
    probe().close();
    probe().close();
}

} // namespace
