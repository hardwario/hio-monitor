#pragma once

// Internal to libs/core and its tests: the schema migration mechanism of hio::Settings.
// Production code uses Settings::openDefault()/openFile() with the built-in migration list
// (empty in M0); tests inject their own list and target schema here.

#include "hio/core/result.h"
#include "hio/core/settings.h"

#include <filesystem>
#include <memory>
#include <span>

class QSettings;

namespace hio::detail {

// Upgrades a file from schema (toVersion - 1) to toVersion. Must not touch "meta/schemaVersion".
struct SettingsMigration {
    int toVersion;
    Status (*apply)(QSettings& settings);
};

struct SettingsAccess {
    // Opens `settings` and brings it to `targetSchema` using `migrations` (exactly one per schema
    // step; a missing step is an `internal.unexpected` error). A failing migration leaves the file
    // unchanged.
    [[nodiscard]] static Result<Settings> open(std::unique_ptr<QSettings> settings,
                                               int targetSchema,
                                               std::span<const SettingsMigration> migrations);
};

// Same as Settings::openFile() with an explicit target schema and migration list.
[[nodiscard]] Result<Settings> openSettingsFile(const std::filesystem::path& file, int targetSchema,
                                                std::span<const SettingsMigration> migrations);

} // namespace hio::detail
