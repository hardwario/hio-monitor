#include "hio/device/ble_lock.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/probe_lock.h"
#include "hio/hw/ble/types.h"
#include "lock_file.h"

#include <QCoreApplication>
#include <QDir>
#include <QLockFile>
#include <QString>

#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hio::device {

namespace {

namespace fs = std::filesystem;

std::string lockKey(std::string_view id) { return "ble-" + sanitisedBleId(id); }

fs::path lockPath(std::string_view id, const fs::path& directory) {
    return directory / (lockKey(id) + ".lock");
}

Error inUseError(const ble::BleDeviceId& id, const ProbeLockOwner& owner, const fs::path& path) {
    Error error =
        makeError(ErrorCode::BleInUseByOtherProcess,
                  std::format("lock held by {} (PID {}): {}", owner.app, owner.pid, toUtf8(path)));
    error.message = QCoreApplication::translate(
                        "hio::device", "The device %1 is used by another program (%2, PID %3).")
                        .arg(toQString(id), toQString(owner.app))
                        .arg(owner.pid)
                        .toStdString();
    if (owner.pid == 0) {
        error.suggestion =
            QCoreApplication::translate("hio::device",
                                        "If no other HARDWARIO program is running, delete %1.")
                .arg(QDir::toNativeSeparators(toQString(toUtf8(path))))
                .toStdString();
    }
    return error;
}

} // namespace

std::string sanitisedBleId(std::string_view id) {
    std::string name(id);
    for (char& c : name) {
        const bool keep =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!keep) {
            c = '-';
        }
    }
    return name;
}

fs::path BleLock::defaultDirectory() { return ProbeLock::defaultDirectory(); }

Result<BleLock> BleLock::acquire(const ble::BleDeviceId& id, const fs::path& directory) {
    const detail::LockErrors errors{
        .inUse = [&id](const ProbeLockOwner& owner,
                       const fs::path& path) { return inUseError(id, owner, path); },
        .heldByThisProcess =
            [&id] {
                // Q3 (plan 0011, 2026-10-19): the same code as another process, exit 5.
                Error error = makeError(ErrorCode::BleInUseByOtherProcess,
                                        "the device lock is held by this process");
                error.message =
                    QCoreApplication::translate(
                        "hio::device", "The device %1 is already used by this application.")
                        .arg(toQString(id))
                        .toStdString();
                return error;
            }};
    auto lock = detail::acquireLockFile(directory, lockPath(id, directory), lockKey(id), errors);
    if (!lock) {
        return std::unexpected(std::move(lock).error());
    }
    return BleLock(id, std::move(*lock));
}

std::optional<ProbeLockOwner> BleLock::owner(std::string_view id, const fs::path& directory) {
    return detail::lockFileOwner(lockPath(id, directory), lockKey(id));
}

BleLock::BleLock(ble::BleDeviceId id, std::unique_ptr<QLockFile> lock)
    : m_id(std::move(id)), m_lock(std::move(lock)) {}

BleLock::BleLock(BleLock&& other) noexcept = default;

BleLock& BleLock::operator=(BleLock&& other) noexcept {
    if (this != &other) {
        release();
        m_id = std::move(other.m_id);
        m_lock = std::move(other.m_lock);
    }
    return *this;
}

BleLock::~BleLock() { release(); }

void BleLock::release() { detail::releaseLockFile(m_lock, lockKey(m_id)); }

} // namespace hio::device
