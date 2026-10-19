#pragma once

// QtBleApi - IBleApi on QtBluetooth, the only code that touches QtBluetooth (ADR-0080, ADR-0087,
// plan/features/0011). Lives on the BLE thread: every QtBluetooth object is created, used and
// destroyed there. Controllers (and their service objects, Qt children) are children of this
// object and released with deleteLater() on disconnect (never deleted twice).
//
// Covered by HIL and manual checks only (ADR-0087); the link error mapping is in one function,
// detail::mapLinkError(), so it can be adjusted in one place after HIL (review B7).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/hw/ble/ble_api.h"
#include "hio/hw/ble/types.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QObject>
#include <QPointer>
#include <QString>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

class QBluetoothLocalDevice;

namespace hio::ble {

// The mapping of QtBluetooth results (plan 0011; kept here so that HIL findings change one
// place, and unit-tested for the legacy defects).
namespace detail {
// A bit test of the RX properties (legacy bug: equality): write-without-response whenever the
// characteristic allows it, also when it allows both.
[[nodiscard]] WriteMode writeModeFor(QLowEnergyCharacteristic::PropertyTypes properties);
enum class LinkPhase : std::uint8_t {
    Connecting,     // connect, service and characteristic discovery
    PairingPending, // the CCCD write is pending (an unbonded link pairs now)
    Ready,          // TX notifications enabled; RX writes
};
// A QtBluetooth error of a link: from the controller or from the NUS service object.
struct QtLinkError {
    std::optional<QLowEnergyController::Error> controller;
    std::optional<QLowEnergyService::ServiceError> service;
    LinkPhase phase = LinkPhase::Connecting;
};
// ble.not_paired only with the platform's authentication/encryption detail (ADR-0084, review B7).
[[nodiscard]] Error mapLinkError(const QtLinkError& error);
[[nodiscard]] Error mapDiscoveryError(QBluetoothDeviceDiscoveryAgent::Error error,
                                      const QString& text);
} // namespace detail

class QtBleApi final : public QObject, public IBleApi {
    Q_OBJECT

public:
    // Devices remembered from scans (their QBluetoothDeviceInfo is needed to connect, e.g. the
    // CoreBluetooth UUID on macOS).
    static constexpr std::size_t maxKnownDevices = 256;

    QtBleApi();
    ~QtBleApi() override;
    QtBleApi(const QtBleApi&) = delete;
    QtBleApi& operator=(const QtBleApi&) = delete;
    QtBleApi(QtBleApi&&) = delete;
    QtBleApi& operator=(QtBleApi&&) = delete;

    void setListener(IBleListener* listener) override;
    [[nodiscard]] AdapterInfo adapter() override;
    [[nodiscard]] Status startScan(std::chrono::milliseconds duration) override;
    void stopScan() override;
    [[nodiscard]] Result<LinkId> connect(const BleDeviceId& device, const NusUuids& uuids) override;
    [[nodiscard]] Status write(LinkId id, std::span<const std::byte> chunk, WriteMode mode,
                               std::uint64_t token) override;
    void disconnect(LinkId id) override;
    [[nodiscard]] Status requestPairing(const BleDeviceId& device) override;
    [[nodiscard]] bool isPaired(const BleDeviceId& device) override;

private:
    struct Link {
        BleDeviceId device;
        NusUuids uuids;
        QPointer<QLowEnergyController> controller; // child of this; deleteLater on disconnect
        QPointer<QLowEnergyService> service;       // child of the controller
        QLowEnergyCharacteristic rx;
        QLowEnergyCharacteristic tx;
        WriteMode mode = WriteMode::WithResponse;
        bool cccdPending = false;
        bool ready = false;
        std::deque<std::uint64_t> writes; // with-response tokens in write order
    };

    void remember(const QBluetoothDeviceInfo& info);
    void advertisement(const QBluetoothDeviceInfo& info);
    void servicesDiscovered(LinkId id);
    void detailsDiscovered(LinkId id);
    void lost(LinkId id, Error error);
    [[nodiscard]] static detail::LinkPhase phaseOf(const Link& link);
    void release(Link& link);
    [[nodiscard]] Link* find(LinkId id);
    [[nodiscard]] QBluetoothLocalDevice& local();

    IBleListener* m_listener = nullptr;
    std::unique_ptr<QBluetoothLocalDevice> m_local;
    std::unique_ptr<QBluetoothDeviceDiscoveryAgent> m_agent;
    bool m_scanning = false;
    std::map<BleDeviceId, QBluetoothDeviceInfo> m_known; // <= maxKnownDevices
    std::deque<BleDeviceId> m_knownOrder;                // oldest first
    std::map<LinkId, Link> m_links;
    std::vector<BleDeviceId> m_pairing; // devices with a pairing request in progress
    LinkId m_nextLink = 1;
};

// The BleDeviceId of a discovered device (address, or the CoreBluetooth UUID on macOS).
[[nodiscard]] BleDeviceId deviceIdOf(const QBluetoothDeviceInfo& info);

} // namespace hio::ble
