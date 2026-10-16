#pragma once

// Application settings (ADR-0025): QSettings INI format, user scope, on all OSes
// (HARDWARIO/Monitor.ini under the platform config directory), shared by the GUI and the CLI.
//
// Schema: "meta/schemaVersion". On open: missing -> written as currentSettingsSchema; lower ->
// the ordered migrations run; higher (file written by a newer version) -> opened read-only, never
// overwritten, and a `settings.newer_schema` warning is logged.
// Thread safety: a Settings object is owned by one thread (documented by its owner). Several
// processes may open the same file (QSettings file locking).
//
// Supported value types: bool, int, std::string (UTF-8), std::filesystem::path.

#include "hio/core/result.h"

#include <QString>
#include <QVariant>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

class QSettings;

namespace hio {

namespace detail {
struct SettingsAccess;
} // namespace detail

inline constexpr int currentSettingsSchema = 1;

template <class T> struct SettingKey {
    std::string_view path; // "group/key"
    T defaultValue;
};

class Settings {
public:
    // The user's settings file (requires initApplicationIdentity()).
    [[nodiscard]] static Result<Settings> openDefault();
    // The path of the user's settings file (requires initApplicationIdentity(); it may not exist
    // yet). Used by the diagnostic report (plan/features/0007).
    [[nodiscard]] static std::filesystem::path defaultFilePath();
    // An explicit INI file (tests, diagnostics).
    [[nodiscard]] static Result<Settings> openFile(const std::filesystem::path& file);

    Settings(Settings&& other) noexcept;
    Settings& operator=(Settings&& other) noexcept;
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;
    ~Settings();

    // Returns the default value when the key is missing or holds a value of another type.
    template <class T> [[nodiscard]] T get(const SettingKey<T>& key) const {
        static_assert(isSupported<T>(), "unsupported setting type");
        const QVariant raw = value(key.path);
        if (!raw.isValid()) {
            return key.defaultValue;
        }
        std::optional<T> converted;
        if constexpr (std::is_same_v<T, bool>) {
            converted = toBool(raw);
        } else if constexpr (std::is_same_v<T, int>) {
            converted = toInt(raw);
        } else if constexpr (std::is_same_v<T, std::string>) {
            converted = toString(raw);
        } else {
            converted = toPath(raw);
        }
        return converted.value_or(key.defaultValue);
    }

    // Errors: `settings.newer_schema` when read-only, `io.write_failed` when the file is not
    // writable. The value is written to disk on sync() or when the object is destroyed.
    template <class T> [[nodiscard]] Status set(const SettingKey<T>& key, const T& newValue) {
        static_assert(isSupported<T>(), "unsupported setting type");
        if constexpr (std::is_same_v<T, std::filesystem::path>) {
            return setValue(key.path, fromPath(newValue));
        } else if constexpr (std::is_same_v<T, std::string>) {
            return setValue(key.path, fromString(newValue));
        } else {
            return setValue(key.path, QVariant(newValue));
        }
    }

    // Removes the group (e.g. "devices/801234567") and every key under it (ADR-0092); a missing
    // group is no error. Errors: `settings.newer_schema` when read-only, `io.write_failed` when the
    // file is not writable, `usage.invalid_argument` for an empty group or the "meta" group (the
    // schema version). Written to disk on sync() or when the object is destroyed.
    [[nodiscard]] Status remove(std::string_view group);

    // Writes pending changes. Errors: `io.write_failed`, `io.read_failed` (corrupt file).
    [[nodiscard]] Status sync();

    [[nodiscard]] bool isReadOnly() const noexcept { return m_readOnly; }
    // The schema version of the file (may be higher than currentSettingsSchema when read-only).
    [[nodiscard]] int schemaVersion() const noexcept { return m_schemaVersion; }

private:
    friend struct detail::SettingsAccess;

    explicit Settings(std::unique_ptr<QSettings> settings);

    template <class T> static constexpr bool isSupported() {
        return std::is_same_v<T, bool> || std::is_same_v<T, int> ||
               std::is_same_v<T, std::string> || std::is_same_v<T, std::filesystem::path>;
    }

    [[nodiscard]] QVariant value(std::string_view path) const;
    [[nodiscard]] Status setValue(std::string_view path, const QVariant& newValue);

    [[nodiscard]] static std::optional<bool> toBool(const QVariant& raw);
    [[nodiscard]] static std::optional<int> toInt(const QVariant& raw);
    [[nodiscard]] static std::optional<std::string> toString(const QVariant& raw);
    [[nodiscard]] static std::optional<std::filesystem::path> toPath(const QVariant& raw);
    [[nodiscard]] static QVariant fromString(const std::string& text);
    [[nodiscard]] static QVariant fromPath(const std::filesystem::path& path);

    std::unique_ptr<QSettings> m_settings;
    bool m_readOnly = false;
    int m_schemaVersion = 0;
};

} // namespace hio
