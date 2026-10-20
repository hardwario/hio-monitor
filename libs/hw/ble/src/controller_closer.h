#pragma once

// ControllerCloser - deletes a closing QLowEnergyController only after it is unconnected (HIL
// finding H2, 2026-10-20): deleting it in ClosingState leaves BlueZ connected, and the next
// connect fails at the CCCD write. Each closed object is deleted (deleteLater) when its "gone"
// notification arrives (the controller's state became Unconnected) or, at the latest, after
// `bound`; a new connect to the same device waits for that (whenClosed()). waitAll() runs a
// local event loop until nothing is closing or the bound passed (QtBleApi destruction:
// BleHost::stop()).
//
// Thread safety: the BLE thread only. The objects are Qt children of their owner (QtBleApi);
// the closer only schedules their deleteLater().

#include "hio/hw/ble/types.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace hio::ble {

class ControllerCloser {
public:
    // Connects `onGone` to the object's "now unconnected" notification with `context` as the
    // receiver context, and returns the connection.
    using Watch =
        std::function<QMetaObject::Connection(QObject* context, std::function<void()> onGone)>;

    static constexpr std::chrono::milliseconds defaultBound{3000};

    explicit ControllerCloser(std::chrono::milliseconds bound = defaultBound);
    ~ControllerCloser();
    ControllerCloser(const ControllerCloser&) = delete;
    ControllerCloser& operator=(const ControllerCloser&) = delete;
    ControllerCloser(ControllerCloser&&) = delete;
    ControllerCloser& operator=(ControllerCloser&&) = delete;

    // `object` was asked to disconnect; `unconnected`: it is unconnected already (deleted at
    // once). Not owning: `object` stays a Qt child of its owner until deleteLater().
    void close(QObject* object, const BleDeviceId& device, bool unconnected, const Watch& watch);
    [[nodiscard]] bool isClosing(const BleDeviceId& device) const;
    [[nodiscard]] std::size_t closingCount() const noexcept { return m_entries.size(); }
    // Runs `then` once `device` has no closing object (at once when it has none).
    void whenClosed(const BleDeviceId& device, std::function<void()> then);
    // Waits (local event loop) until nothing is closing, at most the bound; then deletes the
    // rest.
    void waitAll();

private:
    struct Entry {
        QPointer<QObject> object;
        BleDeviceId device;
        QMetaObject::Connection connection;
        std::uint64_t serial = 0;
    };
    struct Waiter {
        BleDeviceId device;
        std::function<void()> then;
    };

    void finish(std::uint64_t serial, bool expired);
    void runWaiters();

    std::chrono::milliseconds m_bound;
    std::unique_ptr<QObject> m_context; // receiver of the watches and timers
    std::vector<Entry> m_entries;       // bounded by the links (BleHost::maxLinks) + closing ones
    std::vector<Waiter> m_waiters;      // bounded by the links
    std::uint64_t m_nextSerial = 1;
};

} // namespace hio::ble
