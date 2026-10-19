#pragma once

// BleLock - a cross-process lock of one Bluetooth device (ADR-0083, as ProbeLock ADR-0058; plan
// 0011 B2), RAII and move-only.
//
// QLockFile <AppLocalData>/locks/ble-<sanitised id>.lock (sanitisedBleId()), taken with 0 wait;
// stale locks (the owner process is gone) are taken over. The same stale, own-PID and retry rules
// as ProbeLock (they share the implementation).
//
// Thread safety: a BleLock object belongs to one thread at a time (it may be moved to another).

#include "hio/core/result.h"
#include "hio/device/probe_lock.h"
#include "hio/hw/ble/types.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

class QLockFile;

namespace hio::device {

// The id as used in file names: [A-Za-z0-9-] kept, everything else -> '-' (idempotent).
[[nodiscard]] std::string sanitisedBleId(std::string_view id);

class BleLock {
public:
    // ProbeLock::defaultDirectory().
    [[nodiscard]] static std::filesystem::path defaultDirectory();

    // Errors: ble.in_use_by_other_process (another process; with its app name and PID, or "?" /
    // 0 for an unreadable lock file whose path the suggestion names), ble.in_use_by_other_process
    // "already used by this application" (this process holds it, Q3), io.write_failed.
    [[nodiscard]] static Result<BleLock> acquire(const ble::BleDeviceId& id,
                                                 const std::filesystem::path& directory);
    // The live owner without taking the lock (ProbeLock::owner() rules). `id` may be the device
    // id or its sanitised form (the console file retention passes the latter).
    [[nodiscard]] static std::optional<ProbeLockOwner>
    owner(std::string_view id, const std::filesystem::path& directory);

    BleLock(BleLock&& other) noexcept;
    BleLock& operator=(BleLock&& other) noexcept;
    BleLock(const BleLock&) = delete;
    BleLock& operator=(const BleLock&) = delete;
    ~BleLock();

    [[nodiscard]] const ble::BleDeviceId& deviceId() const noexcept { return m_id; }
    void release();

private:
    BleLock(ble::BleDeviceId id, std::unique_ptr<QLockFile> lock);

    ble::BleDeviceId m_id;
    std::unique_ptr<QLockFile> m_lock;
};

} // namespace hio::device
