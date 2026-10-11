#pragma once

// ProbeLock - a cross-process lock of one J-Link probe (ADR-0058), RAII and move-only.
//
// QLockFile <AppLocalData>/locks/probe-<sn>.lock, taken with 0 wait; stale locks (the owner
// process is gone) are taken over by QLockFile. The lock lives as long as the session that uses
// the probe. Programs that do not use these locks (JLinkExe, nRF Util, the old Monitor) are not
// detected.
//
// Thread safety: a ProbeLock object belongs to one thread at a time (it may be moved to another).

#include "hio/core/result.h"

#include <QtTypes>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

class QLockFile;

namespace hio::device {

struct ProbeLockOwner {
    std::string
        app;        // executable name of the owner as QLockFile records it, e.g. "hio"; "?" unknown
    qint64 pid = 0; // 0 = unknown (an unreadable lock file)
    // owner() only: the lock file's last write time, i.e. about when the owner took the lock
    // (QLockFile writes the file once, when it locks).
    std::optional<std::filesystem::file_time_type> lockedAt;
};

class ProbeLock {
public:
    // <AppLocalData>/locks (requires initApplicationIdentity()).
    [[nodiscard]] static std::filesystem::path defaultDirectory();

    // Errors: probe.in_use_by_other_process (with the owner's app name and PID),
    // probe.open_failed (this process holds it already), io.write_failed (the lock directory
    // cannot be created or the lock file cannot be written).
    [[nodiscard]] static Result<ProbeLock> acquire(std::uint32_t serialNumber);
    [[nodiscard]] static Result<ProbeLock> acquire(std::uint32_t serialNumber,
                                                   const std::filesystem::path& directory);

    // The current owner without taking the lock; nullopt when the probe is not locked or the lock
    // is stale. Stale = exactly what QLockFile::tryLock() would take over (QLockFile has no
    // public non-acquiring stale check, so its rules are mirrored per OS in probe_lock.cpp, from
    // the Qt 6.8.3 sources), plus our own-PID rule (review R15, R37, R41). A lock file that exists
    // but cannot be read is an owner with app "?" and PID 0, as acquire() reports it.
    [[nodiscard]] static std::optional<ProbeLockOwner> owner(std::uint32_t serialNumber);
    [[nodiscard]] static std::optional<ProbeLockOwner>
    owner(std::uint32_t serialNumber, const std::filesystem::path& directory);

    ProbeLock(ProbeLock&& other) noexcept;
    ProbeLock& operator=(ProbeLock&& other) noexcept;
    ProbeLock(const ProbeLock&) = delete;
    ProbeLock& operator=(const ProbeLock&) = delete;
    ~ProbeLock();

    [[nodiscard]] std::uint32_t serialNumber() const noexcept { return m_serialNumber; }
    // Unlocks now (also done by the destructor).
    void release();

private:
    ProbeLock(std::uint32_t serialNumber, std::unique_ptr<QLockFile> lock);

    std::uint32_t m_serialNumber = 0;
    std::unique_ptr<QLockFile> m_lock;
};

} // namespace hio::device
