#include "hio/core/settings.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "settings_internal.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QMetaType>
#include <QSettings>
#include <QString>
#include <QVariant>
#include <QtContainerFwd>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace hio {

namespace {

const QString schemaKey = QStringLiteral("meta/schemaVersion");

// Built-in migrations, ordered by toVersion. M0: none (schema 1 is the first schema).
constexpr std::span<const detail::SettingsMigration> builtInMigrations{};

QString keyOf(std::string_view path) { return toQString(path); }

std::string fileOf(const QSettings& settings) { return toStdString(settings.fileName()); }

Status checkStatus(const QSettings& settings) {
    switch (settings.status()) {
    case QSettings::NoError:
        return {};
    case QSettings::AccessError:
        return std::unexpected(makeError(ErrorCode::IoWriteFailed, fileOf(settings)));
    case QSettings::FormatError:
        return std::unexpected(makeError(ErrorCode::IoReadFailed, fileOf(settings)));
    }
    return std::unexpected(makeError(ErrorCode::IoReadFailed, fileOf(settings)));
}

} // namespace

namespace detail {

namespace {

// The schema version stored in the file; 0 when missing or invalid.
int storedSchema(const QSettings& raw, const QVariant& stored) {
    if (!stored.isValid()) {
        return 0;
    }
    bool ok = false;
    const int version = stored.toInt(&ok);
    if (!ok) {
        qCWarning(log::lcCore) << "Settings: invalid" << schemaKey << "in" << raw.fileName()
                               << "- treating the file as schema 0";
        return 0;
    }
    return version;
}

// Schema 1 is the first schema; a missing or invalid stored version is treated as schema 1.
constexpr int firstSchema = 1;

// Every step base+1 ... targetSchema needs exactly one migration (a missing step would silently
// mark the file as migrated).
Status checkMigrationChain(int fromVersion, int targetSchema,
                           std::span<const SettingsMigration> migrations) {
    for (int version = std::max(fromVersion, firstSchema) + 1; version <= targetSchema; ++version) {
        const auto count = std::ranges::count_if(
            migrations, [version](const SettingsMigration& m) { return m.toVersion == version; });
        if (count != 1) {
            return std::unexpected(makeError(
                ErrorCode::InternalUnexpected,
                std::format("settings: {} migration(s) to schema {}, expected exactly one", count,
                            version)));
        }
    }
    return {};
}

// Restores the exact key/value state captured before a failed migration, so that the QSettings
// destructor never writes a half-migrated file.
void restoreSnapshot(QSettings& raw, const QVariantMap& snapshot) {
    raw.clear();
    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
        raw.setValue(it.key(), it.value());
    }
    raw.sync();
}

// Runs the migrations above `fromVersion` up to `targetSchema` and stores the new version. All
// migrations work on the in-memory state; only a complete success is written. On failure the file
// keeps its previous content and schema version.
Status migrate(QSettings& raw, bool hasStoredVersion, int fromVersion, int targetSchema,
               std::span<const SettingsMigration> migrations) {
    if (hasStoredVersion && fromVersion < targetSchema) {
        if (Status chain = checkMigrationChain(fromVersion, targetSchema, migrations); !chain) {
            return chain;
        }
        QVariantMap snapshot;
        for (const QString& key : raw.allKeys()) {
            snapshot.insert(key, raw.value(key));
        }
        for (int version = std::max(fromVersion, firstSchema) + 1; version <= targetSchema;
             ++version) {
            const auto migration =
                std::ranges::find_if(migrations, [version](const SettingsMigration& m) {
                    return m.toVersion == version;
                });
            qCInfo(log::lcCore) << "Settings: migrating" << raw.fileName() << "to schema"
                                << version;
            if (Status status = migration->apply(raw); !status) {
                qCWarning(log::lcCore) << "Settings: migration to schema" << version
                                       << "failed; the file is left unchanged";
                restoreSnapshot(raw, snapshot);
                return status;
            }
        }
    }
    if (fromVersion != targetSchema || !hasStoredVersion) {
        raw.setValue(schemaKey, targetSchema);
        raw.sync();
        return checkStatus(raw);
    }
    return {};
}

} // namespace

Result<Settings> SettingsAccess::open(std::unique_ptr<QSettings> qsettings, int targetSchema,
                                      std::span<const SettingsMigration> migrations) {
    Settings settings(std::move(qsettings));
    QSettings& raw = *settings.m_settings;

    const QVariant stored = raw.value(schemaKey);
    if (Status status = checkStatus(raw); !status) {
        return std::unexpected(std::move(status).error());
    }
    const int fileVersion = storedSchema(raw, stored);

    if (fileVersion > targetSchema) {
        settings.m_readOnly = true;
        settings.m_schemaVersion = fileVersion;
        const Error warning = makeError(ErrorCode::SettingsNewerSchema, fileOf(raw));
        qCWarning(log::lcCore) << idOf(warning.code).data() << toQString(warning.message)
                               << "schema" << fileVersion << ">" << targetSchema << raw.fileName();
        return settings;
    }

    if (!raw.isWritable()) {
        // Nothing can be written; use the file as it is (set() reports io.write_failed).
        settings.m_schemaVersion = fileVersion;
        qCWarning(log::lcCore) << "Settings file is not writable:" << raw.fileName();
        return settings;
    }

    if (Status status = migrate(raw, stored.isValid(), fileVersion, targetSchema, migrations);
        !status) {
        return std::unexpected(std::move(status).error());
    }
    settings.m_schemaVersion = targetSchema;
    return settings;
}

Result<Settings> openSettingsFile(const std::filesystem::path& file, int targetSchema,
                                  std::span<const SettingsMigration> migrations) {
    auto qsettings = std::make_unique<QSettings>(QString::fromStdU16String(file.u16string()),
                                                 QSettings::IniFormat);
    return SettingsAccess::open(std::move(qsettings), targetSchema, migrations);
}

} // namespace detail

Settings::Settings(std::unique_ptr<QSettings> settings) : m_settings(std::move(settings)) {}

Settings::Settings(Settings&& other) noexcept = default;
Settings& Settings::operator=(Settings&& other) noexcept = default;
Settings::~Settings() = default;

Result<Settings> Settings::openDefault() {
    auto qsettings = std::make_unique<QSettings>(QSettings::IniFormat, QSettings::UserScope,
                                                 QCoreApplication::organizationName(),
                                                 QCoreApplication::applicationName());
    return detail::SettingsAccess::open(std::move(qsettings), currentSettingsSchema,
                                        builtInMigrations);
}

Result<Settings> Settings::openFile(const std::filesystem::path& file) {
    return detail::openSettingsFile(file, currentSettingsSchema, builtInMigrations);
}

Status Settings::sync() {
    if (m_readOnly) {
        return {};
    }
    m_settings->sync();
    return checkStatus(*m_settings);
}

QVariant Settings::value(std::string_view path) const { return m_settings->value(keyOf(path)); }

Status Settings::setValue(std::string_view path, const QVariant& newValue) {
    if (m_readOnly) {
        return std::unexpected(makeError(ErrorCode::SettingsNewerSchema, fileOf(*m_settings)));
    }
    if (!m_settings->isWritable()) {
        return std::unexpected(makeError(ErrorCode::IoWriteFailed, fileOf(*m_settings)));
    }
    m_settings->setValue(keyOf(path), newValue);
    return {};
}

// INI files store every value as text, so values read back are usually strings.
std::optional<bool> Settings::toBool(const QVariant& raw) {
    if (raw.typeId() == QMetaType::Bool) {
        return raw.toBool();
    }
    if (raw.typeId() == QMetaType::QString) {
        const QString text = raw.toString();
        if (text == QStringLiteral("true")) {
            return true;
        }
        if (text == QStringLiteral("false")) {
            return false;
        }
    }
    return std::nullopt;
}

std::optional<int> Settings::toInt(const QVariant& raw) {
    if (raw.typeId() == QMetaType::Int) {
        return raw.toInt();
    }
    if (raw.typeId() == QMetaType::QString) {
        bool ok = false;
        const int value = raw.toString().toInt(&ok);
        if (ok) {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<std::string> Settings::toString(const QVariant& raw) {
    if (raw.typeId() == QMetaType::QString) {
        return toStdString(raw.toString());
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> Settings::toPath(const QVariant& raw) {
    if (raw.typeId() == QMetaType::QString) {
        return std::filesystem::path(raw.toString().toStdU16String());
    }
    return std::nullopt;
}

QVariant Settings::fromString(const std::string& text) { return toQString(text); }

QVariant Settings::fromPath(const std::filesystem::path& path) {
    return QString::fromStdU16String(path.u16string());
}

} // namespace hio
