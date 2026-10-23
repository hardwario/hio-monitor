#pragma once

// JSON Schema builders and argument validation for tool inputs (plan/features/0013 "Argument
// validation").
//
// Supported subset (anything else in an input schema is rejected by Registry::addTool):
//   type (object, string, integer, number, boolean, array), description, title, default,
//   properties, required, additionalProperties (false only; objects built here always have it),
//   enum (strings / numbers), minimum, maximum, minLength, maxLength, pattern, items, minItems,
//   maxItems.
// Validation reports the first offending field (required fields first, then the arguments in key
// order) as a JSON pointer relative to the tools/call params ("/arguments/length"), and fills in
// the "default" of absent optional properties, so handlers receive complete, validated arguments.
//
// Example:
//   const QJsonObject schema = Schema::object()
//       .property("address", Schema::address("Start address."), Required::Yes)
//       .property("length", Schema::integer("Bytes to read.").minimum(1).maximum(65536)
//                               .defaultValue(64))
//       .json();

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <cstdint>
#include <expected>
#include <optional>
#include <utility>

namespace hio::mcp {

// Addresses are strings: hex with 0x (up to 8 digits) or decimal (up to 10 digits), <= 0xFFFFFFFF.
inline constexpr const char* addressPattern = "^(0x[0-9a-fA-F]{1,8}|[0-9]{1,10})$";

// Parses an address matching addressPattern; nullopt for anything else or values above 32 bits.
[[nodiscard]] std::optional<std::uint32_t> parseAddress(QStringView text);
// "0x%08X" (results print addresses this way).
[[nodiscard]] QString formatAddress(std::uint32_t address);

enum class Required : std::uint8_t {
    No,
    Yes,
};

class Schema {
public:
    [[nodiscard]] static Schema object(const QString& description = {});
    [[nodiscard]] static Schema string(const QString& description = {});
    [[nodiscard]] static Schema integer(const QString& description = {});
    [[nodiscard]] static Schema number(const QString& description = {});
    [[nodiscard]] static Schema boolean(const QString& description = {});
    [[nodiscard]] static Schema array(const Schema& items, const QString& description = {});
    // A string with addressPattern.
    [[nodiscard]] static Schema address(const QString& description = {});

    Schema& description(const QString& text);
    Schema& minimum(double value);
    Schema& maximum(double value);
    Schema& minLength(int value);
    Schema& maxLength(int value);
    Schema& pattern(const QString& regex);
    Schema& enumValues(const QStringList& values);
    Schema& defaultValue(const QJsonValue& value);
    Schema& minItems(int value);
    Schema& maxItems(int value);
    // Objects only. additionalProperties is false for every object built here.
    Schema& property(const QString& name, const Schema& schema, Required required = Required::No);

    [[nodiscard]] QJsonObject json() const { return m_json; }

private:
    explicit Schema(QJsonObject json) : m_json(std::move(json)) {}

    QJsonObject m_json;
};

struct ValidationError {
    QString pointer; // JSON pointer of the offending field, e.g. "/arguments/length"
    QString message; // English (ADR-0103)
};

// Checks `arguments` against `schema` (the subset above). `pointerPrefix` is prepended to the
// pointer ("/arguments" for tools/call). On success the arguments with defaults filled in.
[[nodiscard]] std::expected<QJsonObject, ValidationError>
validateArguments(const QJsonObject& schema, const QJsonObject& arguments,
                  const QString& pointerPrefix = QStringLiteral("/arguments"));

// Checks that an input schema only uses the subset above (registration time); an English reason.
[[nodiscard]] std::expected<void, QString> checkSchemaSubset(const QJsonObject& schema);

} // namespace hio::mcp
