#pragma once

// CommandTreeCache - learned device command trees on disk, per firmware (plan/features/0021,
// ADR-0170): AppLocalDataLocation/command-trees/<profile>-<target>-<key hash>.json, keyed by
// (profile, target, fw-name, fw-version from `info show`, ADR-0166), so a known firmware needs no
// `help` query.
//
// Bounds (ADR-0170): a tree is stored only when its JSON has at most maxFileBytes (64 KiB) and the
// tree itself is bounded (CommandTree: 2 000 nodes); at most maxFirmwares (20) files, the least
// recently used (file mtime; load() touches) are deleted after a store. A file that does not parse,
// is too large or belongs to another key counts as a miss.
//
// Several processes (GUI, CLI) may share the directory (like the command history, ADR-0058):
// store() writes atomically (QSaveFile) and evicts under the QLockFile <directory>/.lock (waits at
// most lockTimeout; a timeout skips that store with a log warning, no error). load() needs no lock.
//
// Thread safety: none; one owner thread (GUI thread / CLI main thread).

#include "hio/core/result.h"
#include "hio/device/command_tree.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace hio::device {

struct FirmwareKey {
    std::string profileId; // "chester"
    std::string targetId;  // "app"
    std::string fwName;    // info show fw-name
    std::string fwVersion; // info show fw-version
};

class CommandTreeCache {
public:
    static constexpr std::size_t maxFirmwares = 20;
    static constexpr std::uint64_t maxFileBytes = 64ULL * 1024;
    static constexpr std::chrono::milliseconds lockTimeout{2000};

    explicit CommandTreeCache(std::filesystem::path directory);

    // AppLocalDataLocation/command-trees (requires initApplicationIdentity()).
    [[nodiscard]] static std::filesystem::path defaultDirectory();

    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return m_directory; }
    [[nodiscard]] std::filesystem::path fileFor(const FirmwareKey& firmware) const;

    // The stored tree of `firmware` (touched for the LRU), nullopt when none or unusable.
    [[nodiscard]] std::optional<CommandTree> load(const FirmwareKey& firmware) const;
    // Stores `tree` for `firmware` and evicts the least recently used files beyond maxFirmwares.
    // Errors: io.write_failed; usage.invalid_argument (the JSON is larger than maxFileBytes).
    [[nodiscard]] Status store(const FirmwareKey& firmware, const CommandTree& tree);

    // Lock wait used by store() (tests shorten it).
    void setLockTimeout(std::chrono::milliseconds timeout) noexcept { m_lockTimeout = timeout; }

private:
    void evict(const std::filesystem::path& kept);

    std::filesystem::path m_directory;
    std::chrono::milliseconds m_lockTimeout = lockTimeout;
};

} // namespace hio::device
