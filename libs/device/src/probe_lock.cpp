#include "hio/device/probe_lock.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "lock_file.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLockFile>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QString>
#include <QSysInfo>
#include <QtLogging>
#include <QtTypes>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#ifdef _WIN32
// NOMINMAX and WIN32_LEAN_AND_MEAN are set by libs/device/CMakeLists.txt.
#include <windows.h>
// <psapi.h> needs <windows.h> first.
#include <QtEnvironmentVariables>

#include <array>
#include <psapi.h>
#else
#include <csignal> // NOLINT(misc-include-cleaner): kill() is declared by <signal.h>
#ifdef __APPLE__
#include <array>
#include <libproc.h>
#endif
#endif

namespace hio::device {

namespace {

namespace fs = std::filesystem;

fs::path lockPath(std::uint32_t serialNumber, const fs::path& directory) {
    return directory / std::format("probe-{}.lock", serialNumber);
}

std::string lockKey(std::uint32_t serialNumber) { return std::format("probe-{}", serialNumber); }

Error inUseError(std::uint32_t serialNumber, const ProbeLockOwner& owner, const fs::path& path) {
    Error error =
        makeError(ErrorCode::ProbeInUseByOtherProcess,
                  std::format("lock held by {} (PID {}): {}", owner.app, owner.pid, toUtf8(path)));
    error.message = QCoreApplication::translate(
                        "hio::device", "The J-Link %1 is used by another program (%2, PID %3).")
                        .arg(serialNumber)
                        .arg(toQString(owner.app))
                        .arg(owner.pid)
                        .toStdString();
    if (owner.pid == 0) {
        // An empty or unreadable lock file (e.g. left by a crash while it was being written): not
        // stale for QLockFile, so it is not removed automatically; the user is told which file it
        // is (review R53).
        error.suggestion =
            QCoreApplication::translate("hio::device",
                                        "If no other HARDWARIO program is running, delete %1.")
                .arg(QDir::toNativeSeparators(toQString(toUtf8(path))))
                .toStdString();
    }
    return error;
}

} // namespace

fs::path ProbeLock::defaultDirectory() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return fs::path(base.toStdU16String()) / "locks";
}

Result<ProbeLock> ProbeLock::acquire(std::uint32_t serialNumber) {
    return acquire(serialNumber, defaultDirectory());
}

Result<ProbeLock> ProbeLock::acquire(std::uint32_t serialNumber, const fs::path& directory) {
    const detail::LockErrors errors{
        .inUse =
            [serialNumber](const ProbeLockOwner& owner, const fs::path& path) {
                return inUseError(serialNumber, owner, path);
            },
        .heldByThisProcess =
            [serialNumber] {
                Error error =
                    makeError(ErrorCode::ProbeOpenFailed, "the probe lock is held by this process");
                error.message =
                    QCoreApplication::translate(
                        "hio::device", "The J-Link %1 is already used by this application.")
                        .arg(serialNumber)
                        .toStdString();
                return error;
            }};
    auto lock = detail::acquireLockFile(directory, lockPath(serialNumber, directory),
                                        lockKey(serialNumber), errors);
    if (!lock) {
        return std::unexpected(std::move(lock).error());
    }
    return ProbeLock(serialNumber, std::move(*lock));
}

std::optional<ProbeLockOwner> ProbeLock::owner(std::uint32_t serialNumber) {
    return owner(serialNumber, defaultDirectory());
}

std::optional<ProbeLockOwner> ProbeLock::owner(std::uint32_t serialNumber,
                                               const fs::path& directory) {
    return detail::lockFileOwner(lockPath(serialNumber, directory), lockKey(serialNumber));
}

ProbeLock::ProbeLock(std::uint32_t serialNumber, std::unique_ptr<QLockFile> lock)
    : m_serialNumber(serialNumber), m_lock(std::move(lock)) {}

ProbeLock::ProbeLock(ProbeLock&& other) noexcept = default;

ProbeLock& ProbeLock::operator=(ProbeLock&& other) noexcept {
    if (this != &other) {
        release();
        m_serialNumber = other.m_serialNumber;
        m_lock = std::move(other.m_lock);
    }
    return *this;
}

ProbeLock::~ProbeLock() { release(); }

void ProbeLock::release() { detail::releaseLockFile(m_lock, lockKey(m_serialNumber)); }

} // namespace hio::device
