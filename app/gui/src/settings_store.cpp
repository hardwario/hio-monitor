#include "settings_store.h"

#include "gui_log.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/core/settings.h"

#include <QAnyStringView>
#include <QLoggingCategory>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QStringView>
#include <QVariant>
#include <QtAssert>
#include <QtContainerFwd>
#include <QtTypes>

#include <algorithm>
#include <cmath>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace hio::gui {

namespace validators {

std::function<bool(const QVariant&)> intRange(int min, int max, bool orZero) {
    return [min, max, orZero](const QVariant& value) {
        const int number = value.toInt();
        return (orZero && number == 0) || (number >= min && number <= max);
    };
}

std::function<bool(const QVariant&)> oneOf(QStringList allowed) {
    return [allowed = std::move(allowed)](const QVariant& value) {
        return allowed.contains(value.toString());
    };
}

std::function<bool(const QVariant&)> maxLength(qsizetype length) {
    return [length](const QVariant& value) { return value.toString().size() <= length; };
}

} // namespace validators

namespace {

// "probes/*/speedKhz" matches "probes/683012345/speedKhz".
bool matches(QStringView pattern, QStringView key) {
    if (!pattern.contains(QLatin1Char('*'))) {
        return pattern == key;
    }
    const auto patternParts = pattern.split(QLatin1Char('/'));
    const auto keyParts = key.split(QLatin1Char('/'));
    if (patternParts.size() != keyParts.size()) {
        return false;
    }
    for (qsizetype i = 0; i < patternParts.size(); ++i) {
        if (patternParts[i] == QStringLiteral("*")) {
            if (keyParts[i].isEmpty()) {
                return false;
            }
        } else if (patternParts[i] != keyParts[i]) {
            return false;
        }
    }
    return true;
}

Error invalid(const QString& key, const QString& why) {
    return makeError(ErrorCode::UsageInvalidArgument,
                     toStdString(QStringLiteral("setting %1: %2").arg(key, why)));
}

} // namespace

SettingsStore::SettingsStore(std::optional<Settings> settings, QObject* parent)
    : QObject(parent), m_settings(std::move(settings)) {}

void SettingsStore::registerKeys(std::span<const SettingKey> keys) {
    for (const SettingKey& key : keys) {
        const bool duplicate = std::ranges::any_of(
            m_keys, [&key](const SettingKey& known) { return known.path == key.path; });
        if (duplicate) {
            qCCritical(lcGui) << "SettingsStore: the key" << key.path
                              << "is registered twice; ignored";
            Q_ASSERT_X(false, "SettingsStore::registerKeys", "duplicate setting key");
            continue;
        }
        m_keys.push_back(key);
    }
}

const SettingKey* SettingsStore::find(const QString& key) const {
    // Exact paths first, then patterns.
    const auto exact =
        std::ranges::find_if(m_keys, [&key](const SettingKey& spec) { return spec.path == key; });
    if (exact != m_keys.end()) {
        return &*exact;
    }
    const auto pattern = std::ranges::find_if(
        m_keys, [&key](const SettingKey& spec) { return matches(spec.path, key); });
    return pattern == m_keys.end() ? nullptr : &*pattern;
}

bool SettingsStore::isRegistered(QAnyStringView key) const {
    return find(key.toString()) != nullptr;
}

bool SettingsStore::readOnly() const { return m_settings && m_settings->isReadOnly(); }

const Settings* SettingsStore::settings() const { return m_settings ? &*m_settings : nullptr; }

std::optional<QVariant> SettingsStore::convert(const SettingKey& spec, const QVariant& raw) {
    switch (spec.type) {
    case QMetaType::Bool:
        if (raw.typeId() == QMetaType::Bool) {
            return raw;
        }
        return std::nullopt;
    case QMetaType::Int: {
        if (raw.typeId() == QMetaType::Int) {
            return raw;
        }
        // QML numbers arrive as double.
        if (raw.typeId() == QMetaType::Double || raw.typeId() == QMetaType::LongLong ||
            raw.typeId() == QMetaType::UInt) {
            bool ok = false;
            const double number = raw.toDouble(&ok);
            if (ok && std::trunc(number) == number && number >= -2147483648.0 &&
                number <= 2147483647.0) {
                return QVariant(static_cast<int>(number));
            }
        }
        return std::nullopt;
    }
    case QMetaType::QString:
        if (raw.typeId() == QMetaType::QString) {
            return raw;
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

QVariant SettingsStore::value(QAnyStringView key) const {
    const QString path = key.toString();
    const SettingKey* spec = find(path);
    if (spec == nullptr) {
        qCWarning(lcGui) << "SettingsStore: read of the unregistered key" << path;
        return {};
    }
    if (!m_settings) {
        return m_memory.value(path, spec->defaultValue);
    }
    const std::string stdPath = toStdString(path);
    QVariant stored;
    switch (spec->type) {
    case QMetaType::Bool:
        stored = m_settings->get(
            hio::SettingKey<bool>{.path = stdPath, .defaultValue = spec->defaultValue.toBool()});
        break;
    case QMetaType::Int:
        stored = m_settings->get(
            hio::SettingKey<int>{.path = stdPath, .defaultValue = spec->defaultValue.toInt()});
        break;
    default:
        stored = toQString(m_settings->get(hio::SettingKey<std::string>{
            .path = stdPath, .defaultValue = toStdString(spec->defaultValue.toString())}));
        break;
    }
    // A stored value that the validator rejects (edited by hand, older build) reads as default.
    if (spec->validator && !spec->validator(stored)) {
        qCWarning(lcGui) << "SettingsStore: invalid stored value of" << path << "- using default";
        return spec->defaultValue;
    }
    return stored;
}

Status SettingsStore::removeGroup(const QString& group) {
    if (!m_settings) {
        const QString prefix = group + QLatin1Char('/');
        m_memory.removeIf([&prefix](const auto& entry) { return entry.key().startsWith(prefix); });
        return {};
    }
    Status status = m_settings->remove(toStdString(group));
    if (status) {
        status = m_settings->sync();
    }
    return status;
}

Status SettingsStore::setValue(QAnyStringView key, const QVariant& newValue) {
    const QString path = key.toString();
    const SettingKey* spec = find(path);
    if (spec == nullptr) {
        return std::unexpected(invalid(path, QStringLiteral("unregistered key")));
    }
    const std::optional<QVariant> converted = convert(*spec, newValue);
    if (!converted) {
        return std::unexpected(
            invalid(path, QStringLiteral("expected type %1, got %2")
                              .arg(QString::fromLatin1(QMetaType(spec->type).name()),
                                   QString::fromLatin1(newValue.metaType().name()))));
    }
    if (spec->validator && !spec->validator(*converted)) {
        return std::unexpected(
            invalid(path, QStringLiteral("value %1 is not allowed").arg(converted->toString())));
    }

    if (!m_settings) {
        m_memory.insert(path, *converted);
        valueChanged(path); // signal
        return {};
    }
    const std::string stdPath = toStdString(path);
    Status status;
    switch (spec->type) {
    case QMetaType::Bool:
        status = m_settings->set(hio::SettingKey<bool>{.path = stdPath, .defaultValue = false},
                                 converted->toBool());
        break;
    case QMetaType::Int:
        status = m_settings->set(hio::SettingKey<int>{.path = stdPath, .defaultValue = 0},
                                 converted->toInt());
        break;
    default:
        status = m_settings->set(hio::SettingKey<std::string>{.path = stdPath, .defaultValue = {}},
                                 toStdString(converted->toString()));
        break;
    }
    if (status) {
        status = m_settings->sync();
    }
    if (!status) {
        return status;
    }
    valueChanged(path); // signal
    return {};
}

} // namespace hio::gui
