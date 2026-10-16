#pragma once

// JSON helpers on top of Qt's JSON classes (ADR-0019) and the CLI envelope (ADR-0026).
//
// Envelopes (one per line on stdout with --json):
//   {"schema":1,"ok":true,"data":<data>}
//   {"schema":1,"ok":false,"error":{"code","category","message","cause","suggestion","detail","wizard"}}
//   check commands (ADR-0070): the error envelope may also carry "data"
// Note: QJsonObject serializes keys in alphabetical order; JSON consumers must not rely on order.

#include "hio/core/error.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace hio {

// Error as a JSON object. Empty strings are kept; "wizard" is null or a kebab-case id.
[[nodiscard]] QJsonObject toJson(const Error& error);

[[nodiscard]] QJsonObject okEnvelope(const QJsonValue& data);
[[nodiscard]] QJsonObject errorEnvelope(const Error& error);
// ADR-0070: the error envelope of a check command also carries "data" (every check result).
[[nodiscard]] QJsonObject errorEnvelope(const Error& error, const QJsonValue& data);

// The kebab-case id of a wizard ("first-run-setup", ADR-0067) and back (nullopt for unknown ids).
[[nodiscard]] std::string_view wizardIdString(WizardId wizard) noexcept;
[[nodiscard]] std::optional<WizardId> wizardIdFromString(std::string_view id) noexcept;

// Compact single-line UTF-8 serialization (no trailing newline).
[[nodiscard]] std::string toCompactUtf8(const QJsonObject& object);

// UTF-8 <-> QString conversions.
[[nodiscard]] QString toQString(std::string_view utf8);
[[nodiscard]] std::string toStdString(const QString& text);

// A path as UTF-8 for messages and logs. Never throws on unusual characters (unlike
// std::filesystem::path::string() on Windows, which throws for characters outside the ANSI code
// page); POSIX paths are returned byte for byte.
[[nodiscard]] std::string toUtf8(const std::filesystem::path& path);

} // namespace hio
