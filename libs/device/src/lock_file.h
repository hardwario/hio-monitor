#pragma once

// The cross-process lock files of ProbeLock and BleLock (ADR-0058, ADR-0083; plan 0011 B2): one
// QLockFile per key, QLockFile's stale rules mirrored for a non-acquiring owner() (review R37,
// R41), the own-PID rule (R15, R20) and the stale retry (R51). Shared implementation; the public
// types are ProbeLock and BleLock.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/probe_lock.h"

#include <QLockFile>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace hio::device::detail {

struct LockErrors {
    // The lock is held by another process (owner "?" / PID 0 when unreadable).
    std::function<Error(const ProbeLockOwner& owner, const std::filesystem::path& path)> inUse;
    // This process holds the lock already.
    std::function<Error()> heldByThisProcess;
};

// Takes the lock file `path` (creating `directory`) for `key` ("probe-<sn>", "ble-<id>").
// Errors: those of `errors`, io.write_failed.
[[nodiscard]] Result<std::unique_ptr<QLockFile>>
acquireLockFile(const std::filesystem::path& directory, const std::filesystem::path& path,
                const std::string& key, const LockErrors& errors);

// The live owner of `path` (nullopt when not locked or stale), see ProbeLock::owner().
[[nodiscard]] std::optional<ProbeLockOwner> lockFileOwner(const std::filesystem::path& path,
                                                          const std::string& key);

// Unlocks and forgets `key` (no-op for nullptr).
void releaseLockFile(std::unique_ptr<QLockFile>& lock, const std::string& key);

} // namespace hio::device::detail
