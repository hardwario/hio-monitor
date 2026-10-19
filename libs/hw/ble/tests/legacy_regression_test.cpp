// Regression tests of the legacy app's Bluetooth defects (plan/features/0011 D2,
// plan/0002-legacy-inventory.md; branch `main`: src/btworker.cpp, src/bluetooth.cpp). Each defect
// and where it is covered:
//   - "sent" reported before the write        NusLinkTest.Chunking (echo after the last chunk),
//                                             BleSessionTest.EchoOnlyAfterTheLastChunk
//   - unbounded message buffer                NusLinkTest.QueueIsBounded, TooLongLineIsRejected,
//                                             EventChannel.DropsOnlyDroppableEventsWhenFull,
//                                             BleSessionTest.EventOverflowIsReportedAsDroppedLines
//   - null dereference in the discovery       BleScanner.DiscoveryErrorWithoutAnyLink
//     error handler
//   - devices de-duplicated by name           BleScanner.DevicesWithEqualNamesAreKeptApart
//   - write type chosen with == (bit test)    LegacyBle.WriteModeIsABitTest (here)
//   - MTU ignored, no chunking                NusLinkTest.Chunking.*
//   - no reconnect                            BleSessionTest.ReconnectsOnlyWhileTheDeviceIsSeen
//   - controller deleted twice                QtBleApi (QPointer + one deleteLater); HIL only
//   - worker on the GUI thread                BleHost.ApiIsCreatedUsedAndDestroyedOnTheBleThread
//   - any service error "probably unpaired"   LegacyBle.NotPairedOnlyWithAnAuthenticationDetail

#include "hio/core/error.h"
#include "hio/hw/ble/types.h"
#include "qt_ble_api.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QString>

#include <gtest/gtest.h>

#include <optional>

namespace {

using hio::ErrorCode;
using hio::ble::WriteMode;
using hio::ble::detail::writeModeFor;

TEST(LegacyBle, WriteModeIsABitTest) {
    using Property = QLowEnergyCharacteristic::PropertyType;
    EXPECT_EQ(writeModeFor(Property::Write), WriteMode::WithResponse);
    EXPECT_EQ(writeModeFor(Property::WriteNoResponse), WriteMode::WithoutResponse);
    // Both bits (the legacy `==` chose with-response here).
    EXPECT_EQ(writeModeFor(QLowEnergyCharacteristic::PropertyTypes(Property::Write) |
                           Property::WriteNoResponse),
              WriteMode::WithoutResponse);
    EXPECT_EQ(writeModeFor(QLowEnergyCharacteristic::PropertyTypes(Property::WriteNoResponse) |
                           Property::Read | Property::Notify),
              WriteMode::WithoutResponse);
}

// ADR-0084 / review B7: ble.not_paired only with the platform's authentication/encryption detail
// (Qt: AuthorizationError), for the CCCD and the RX write alike; a service error without it is no
// "probably unpaired" (legacy bug).
TEST(LegacyBle, NotPairedOnlyWithAnAuthenticationDetail) {
    using hio::ble::detail::LinkPhase;
    using hio::ble::detail::mapLinkError;
    using hio::ble::detail::QtLinkError;
    const auto service = [](QLowEnergyService::ServiceError error, LinkPhase phase) {
        return mapLinkError(
                   QtLinkError{.controller = std::nullopt, .service = error, .phase = phase})
            .code;
    };
    const auto controller = [](QLowEnergyController::Error error, LinkPhase phase) {
        return mapLinkError(
                   QtLinkError{.controller = error, .service = std::nullopt, .phase = phase})
            .code;
    };
    EXPECT_EQ(service(QLowEnergyService::DescriptorWriteError, LinkPhase::PairingPending),
              ErrorCode::BleConnectFailed);
    EXPECT_EQ(service(QLowEnergyService::CharacteristicWriteError, LinkPhase::Ready),
              ErrorCode::BleWriteFailed);
    EXPECT_EQ(service(QLowEnergyService::OperationError, LinkPhase::Connecting),
              ErrorCode::BleConnectFailed);
    EXPECT_EQ(controller(QLowEnergyController::AuthorizationError, LinkPhase::PairingPending),
              ErrorCode::BleNotPaired);
    EXPECT_EQ(controller(QLowEnergyController::AuthorizationError, LinkPhase::Ready),
              ErrorCode::BleNotPaired);
    EXPECT_EQ(controller(QLowEnergyController::ConnectionError, LinkPhase::Connecting),
              ErrorCode::BleConnectFailed);
    EXPECT_EQ(controller(QLowEnergyController::UnknownRemoteDeviceError, LinkPhase::Connecting),
              ErrorCode::BleConnectFailed);
    EXPECT_EQ(controller(QLowEnergyController::RemoteHostClosedError, LinkPhase::Ready),
              ErrorCode::BleDisconnected);
    EXPECT_EQ(controller(QLowEnergyController::UnknownError, LinkPhase::Ready),
              ErrorCode::BleDisconnected);
    EXPECT_EQ(controller(QLowEnergyController::MissingPermissionsError, LinkPhase::Connecting),
              ErrorCode::BlePermissionDenied);
}

TEST(LegacyBle, DiscoveryErrors) {
    using hio::ble::detail::mapDiscoveryError;
    EXPECT_EQ(mapDiscoveryError(QBluetoothDeviceDiscoveryAgent::PoweredOffError, {}).code,
              ErrorCode::BleAdapterOff);
    EXPECT_EQ(mapDiscoveryError(QBluetoothDeviceDiscoveryAgent::MissingPermissionsError, {}).code,
              ErrorCode::BlePermissionDenied);
    EXPECT_EQ(
        mapDiscoveryError(QBluetoothDeviceDiscoveryAgent::InputOutputError, QStringLiteral("io"))
            .code,
        ErrorCode::BleStackUnavailable);
}

// Review B6: one id form per device.
TEST(LegacyBle, DeviceIdsAreCanonical) {
    EXPECT_EQ(hio::ble::canonicalDeviceId("f1:22:33:44:55:6a"), "F1:22:33:44:55:6A");
    EXPECT_EQ(hio::ble::canonicalDeviceId(" F1:22:33:44:55:6A "), "F1:22:33:44:55:6A");
    EXPECT_EQ(hio::ble::canonicalDeviceId("{6A0B3F1C-1D2E-4F50-8A9B-0C1D2E3F4A5B}"),
              "6a0b3f1c-1d2e-4f50-8a9b-0c1d2e3f4a5b");
    EXPECT_EQ(hio::ble::canonicalDeviceId("6A0B3F1C-1D2E-4F50-8A9B-0C1D2E3F4A5B"),
              "6a0b3f1c-1d2e-4f50-8a9b-0c1d2e3f4a5b");
    EXPECT_EQ(hio::ble::canonicalDeviceId("not-an-id"), "not-an-id");
    EXPECT_TRUE(hio::ble::sameDeviceId("aa:bb", "AA:BB"));
    EXPECT_FALSE(hio::ble::sameDeviceId("aa:bb", "AA:BC"));
}

} // namespace
