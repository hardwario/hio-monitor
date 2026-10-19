#pragma once

// BleScanner - one scan of nearby BLE devices, as a list for the CLI and the GUI picker
// (ADR-0083, ADR-0129; plan/features/0011).
//
// Rows are de-duplicated by device id (never by name: legacy bug), hold at most maxDevices (the
// one seen least recently is evicted), names are cut at maxNameBytes (UTF-8 boundary) and sorted
// by RSSI (strongest first). Which profile a device belongs to is decided by the injected matcher
// (libs/device: ProfileRegistry::findByAdvertisement); without `all`, devices without a profile
// are left out. devicesChanged() is emitted at most every signalInterval, finished() once per
// start() when the scan ended (its duration passed, stop() was not called) or failed.
//
// Thread safety: owner thread only (a facade on BleHost, ADR-0080).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/hw/ble/ble_host.h"
#include "hio/hw/ble/types.h"

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hio::ble {

// The id of the profile an advertisement belongs to, "" for none.
using ProfileMatcher = std::function<std::string(const Advertisement&)>;

struct ScanOptions {
    std::chrono::milliseconds duration{5000}; // 0 = until stop()
    ProfileMatcher matcher;
    bool all = false; // keep devices without a profile too (`--all`, "Show all devices")
};

struct BleDeviceInfo {
    BleDeviceId id;
    std::string name; // <= BleScanner::maxNameBytes
    int rssi = 0;
    bool nusAdvertised = false;
    std::string profileId; // "" = no profile
    std::chrono::steady_clock::time_point lastSeen;
};

class BleScanner : public QObject {
    Q_OBJECT

public:
    static constexpr std::size_t maxDevices = 256;
    static constexpr std::size_t maxNameBytes = 64;
    static constexpr std::chrono::milliseconds signalInterval{250}; // <= 4 signals per second

    explicit BleScanner(BleHost& host, QObject* parent = nullptr);
    ~BleScanner() override;
    BleScanner(const BleScanner&) = delete;
    BleScanner& operator=(const BleScanner&) = delete;
    BleScanner(BleScanner&&) = delete;
    BleScanner& operator=(BleScanner&&) = delete;

    // Starts (or restarts) a scan; the list is kept. Errors: those of BleHost::post().
    [[nodiscard]] Status start(ScanOptions options);
    void stop();
    void clear();
    [[nodiscard]] bool isScanning() const noexcept { return m_scanning; }
    // Sorted by RSSI, strongest first.
    [[nodiscard]] const std::vector<BleDeviceInfo>& devices() const noexcept { return m_devices; }

signals:
    void devicesChanged(const QList<hio::ble::BleDeviceInfo>& devices);
    void finished(const std::optional<hio::Error>& error);

private:
    Q_INVOKABLE void drainEvents();
    void add(const Advertisement& advertisement);
    void publish();

    BleHost* m_host; // non-owning; outlives the scanner
    std::shared_ptr<Subscription> m_subscription;
    std::atomic<bool> m_wakePending{false};
    ScanOptions m_options;
    bool m_scanning = false;
    bool m_dirty = false;
    std::vector<BleDeviceInfo> m_devices; // <= maxDevices
    QTimer m_signalTimer;
};

// `text` cut to at most `maxBytes` bytes without splitting a UTF-8 character.
[[nodiscard]] std::string truncateUtf8(std::string text, std::size_t maxBytes);

} // namespace hio::ble

Q_DECLARE_METATYPE(hio::ble::BleDeviceInfo)
