#pragma once

// IBleApi - the only seam to the platform Bluetooth stack (ADR-0087, plan/features/0011).
// Implemented by QtBleApi (QtBluetooth, the only code that touches it) and, in builds with
// HIO_TEST_OVERRIDES, by the scenario-driven FakeBleApi (ADR-0127); GMock'd in unit tests.
//
// Thread safety: an IBleApi and its listener are used on the BLE thread only (ADR-0080). The
// implementation creates, uses and destroys all its platform objects there. Listener calls happen
// on that thread from the event loop, never from inside an IBleApi call (except where noted), and
// never throw.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/hw/ble/types.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace hio::ble {

class IBleListener {
public:
    IBleListener() = default;
    IBleListener(const IBleListener&) = delete;
    IBleListener& operator=(const IBleListener&) = delete;
    IBleListener(IBleListener&&) = delete;
    IBleListener& operator=(IBleListener&&) = delete;
    virtual ~IBleListener() = default;

    virtual void onAdvertisement(const Advertisement& advertisement) = 0;
    // The scan ended (its duration passed, or an error); not after stopScan().
    virtual void onScanFinished(std::optional<hio::Error> error) = 0;
    // `waitingForPairing`: the CCCD write is pending (an unbonded link pairs now).
    virtual void onLinkConnecting(LinkId link, bool waitingForPairing) = 0;
    virtual void onLinkReady(LinkId link, LinkReady ready) = 0;
    virtual void onLinkData(LinkId link, std::span<const std::byte> data) = 0;
    // A write() finished: confirmed (with response) or handed to the stack (without response).
    virtual void onWriteDone(LinkId link, std::uint64_t token, std::optional<hio::Error> error) = 0;
    // The link failed or was lost (not after disconnect()).
    virtual void onLinkLost(LinkId link, hio::Error error) = 0;
    virtual void onPairingFinished(BleDeviceId device, Status status) = 0;
};

class IBleApi {
public:
    IBleApi() = default;
    IBleApi(const IBleApi&) = delete;
    IBleApi& operator=(const IBleApi&) = delete;
    IBleApi(IBleApi&&) = delete;
    IBleApi& operator=(IBleApi&&) = delete;
    virtual ~IBleApi() = default;

    virtual void setListener(IBleListener* listener) = 0; // non-owning; nullptr = none
    // Reads the adapter and permission state; never triggers the macOS permission prompt.
    [[nodiscard]] virtual AdapterInfo adapter() = 0;
    // 0 = until stopScan(). Errors: the adapter's (adapterError()).
    [[nodiscard]] virtual Status startScan(std::chrono::milliseconds duration) = 0;
    virtual void stopScan() = 0;
    // connect -> service discovery -> characteristic discovery -> CCCD enable -> MTU; then
    // onLinkReady, or onLinkLost with the error.
    [[nodiscard]] virtual Result<LinkId> connect(const BleDeviceId& device,
                                                 const NusUuids& uuids) = 0;
    [[nodiscard]] virtual Status write(LinkId link, std::span<const std::byte> chunk,
                                       WriteMode mode, std::uint64_t token) = 0;
    virtual void disconnect(LinkId link) = 0; // idempotent; no onLinkLost afterwards
    // macOS: ble.pairing_not_supported.
    [[nodiscard]] virtual Status requestPairing(const BleDeviceId& device) = 0;
    // Whether the system has a bond with the device (Q4: "Already paired."); false when unknown.
    [[nodiscard]] virtual bool isPaired(const BleDeviceId& device) = 0;
};

// QtBleApi, or FakeBleApi when built with HIO_TEST_OVERRIDES and HIO_FAKE_BLE_SCENARIO is set
// (ADR-0087, ADR-0127). Call on the BLE thread.
[[nodiscard]] std::unique_ptr<IBleApi> createBleApi();

} // namespace hio::ble
