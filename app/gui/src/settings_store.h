#pragma once

// SettingsStore - the GUI's single writer of the settings (plan/features/0004-m3-gui-shell.md, A5;
// ADR-0025, ADR-0058).
//
// A QObject over hio::Settings with a key table: every key a GUI plan uses is registered with its
// type, default and validator (registerKeys; 0004 registers the ui/*, jlink/*, rtt/* and probes/*
// keys in AppViewModel, 0005 and 0006 register theirs). setValue() validates the value against the
// table (usage.invalid_argument; nothing is written), writes it, calls sync() at once (ADR-0058:
// other processes see it) and emits valueChanged(key). A file written by a newer schema is opened
// read-only (readOnly(), writes fail with settings.newer_schema). When the settings file could not
// be opened at all, values live in memory for this run only.
//
// Schema: the keys of 0004 are new keys with defaults, so files of schema 1 need no migration and
// the schema version stays 1 (a "no-op migration"); older builds ignore the keys they do not know.
//
// Key paths: "group/key"; a "*" segment matches exactly one segment ("probes/*/speedKhz").
//
// Ownership: owned by AppViewModel (Qt parent). Thread safety: GUI thread only.

#include "hio/core/result.h"
#include "hio/core/settings.h"

#include <QAnyStringView>
#include <QHash>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtQmlIntegration>

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace hio::gui {

struct SettingKey {
    QString path;
    QMetaType::Type type = QMetaType::QString; // Bool, Int or QString
    QVariant defaultValue;
    // Optional extra check of the converted value (range, allowed strings, ...).
    std::function<bool(const QVariant&)> validator;
};

// Validators for SettingKey::validator.
namespace validators {
// min <= value <= max; with `orZero` also 0 ("use the profile's value").
[[nodiscard]] std::function<bool(const QVariant&)> intRange(int min, int max, bool orZero = false);
[[nodiscard]] std::function<bool(const QVariant&)> oneOf(QStringList allowed);
[[nodiscard]] std::function<bool(const QVariant&)> maxLength(qsizetype length);
} // namespace validators

class SettingsStore : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppViewModel")

    Q_PROPERTY(bool readOnly READ readOnly CONSTANT)

public:
    // `settings` empty: the file could not be opened; values are kept in memory for this run.
    explicit SettingsStore(std::optional<Settings> settings, QObject* parent = nullptr);

    // The stored value converted to the key's type, or its default. Invalid QVariant for an
    // unregistered key (logged).
    [[nodiscard]] QVariant value(QAnyStringView key) const;
    // Errors: usage.invalid_argument (unregistered key, wrong type, validator),
    // settings.newer_schema (read-only), io.write_failed.
    [[nodiscard]] Status setValue(QAnyStringView key, const QVariant& newValue);
    // Removes a group and every key under it ("probes/683012345", ADR-0092), then sync().
    // Errors: those of hio::Settings::remove (settings.newer_schema, io.write_failed,
    // usage.invalid_argument for an empty group or "meta").
    [[nodiscard]] Status removeGroup(const QString& group);
    // Duplicate paths are a programming error: asserted in debug, ignored and logged in release.
    void registerKeys(std::span<const SettingKey> keys);
    [[nodiscard]] bool isRegistered(QAnyStringView key) const;
    [[nodiscard]] bool readOnly() const;

    // For backend helpers that read hio::Settings directly (jlink::defaultLocatorOptions); nullptr
    // when the file could not be opened.
    [[nodiscard]] const Settings* settings() const;

    // QML access (0006's Settings page uses its own view model; these are for small bindings).
    Q_INVOKABLE [[nodiscard]] QVariant get(const QString& key) const { return value(key); }

signals:
    void valueChanged(const QString& key); // writes of this process

private:
    [[nodiscard]] const SettingKey* find(const QString& key) const;
    [[nodiscard]] static std::optional<QVariant> convert(const SettingKey& spec,
                                                         const QVariant& raw);

    std::optional<Settings> m_settings;
    std::vector<SettingKey> m_keys;
    QHash<QString, QVariant> m_memory; // used only when m_settings is empty
};

} // namespace hio::gui
