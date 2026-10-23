#include "hio/mcp/schema.h"

#include <QByteArrayView>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QStringView>
#include <QtContainerFwd>
#include <QtCore/qcborcommon.h>
#include <QtTypes>

#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <utility>

namespace hio::mcp {

namespace {

constexpr double maxExactInteger = 9007199254740991.0; // 2^53 - 1
// Nesting bound of input schemas (and therefore of validated arguments).
constexpr int maxSchemaDepth = 16;

const QString typeKey = QStringLiteral("type");

QString escapePointerToken(const QString& token) {
    QString escaped = token;
    escaped.replace(QLatin1Char('~'), QStringLiteral("~0"));
    escaped.replace(QLatin1Char('/'), QStringLiteral("~1"));
    return escaped;
}

QString childPointer(const QString& pointer, const QString& token) {
    return pointer + QLatin1Char('/') + escapePointerToken(token);
}

std::unexpected<ValidationError> invalid(const QString& pointer, const QString& message) {
    return std::unexpected(ValidationError{.pointer = pointer, .message = message});
}

QString numberText(double value) { return QString::number(value, 'g', 17); }

bool isInteger(const QJsonValue& value) {
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    return std::isfinite(number) && std::trunc(number) == number &&
           std::fabs(number) <= maxExactInteger;
}

std::expected<void, ValidationError> checkEnum(const QJsonObject& schema, const QJsonValue& value,
                                               const QString& pointer) {
    if (!schema.contains(QStringLiteral("enum"))) {
        return {};
    }
    const QJsonArray allowed = schema.value(QStringLiteral("enum")).toArray();
    for (const auto& candidate : allowed) {
        if (candidate == value) {
            return {};
        }
    }
    QStringList names;
    for (const auto& candidate : allowed) {
        names.append(candidate.isString() ? candidate.toString()
                                          : numberText(candidate.toDouble()));
    }
    return invalid(pointer,
                   QStringLiteral("must be one of: %1").arg(names.join(QStringLiteral(", "))));
}

std::expected<void, ValidationError> checkRange(const QJsonObject& schema, double number,
                                                const QString& pointer) {
    const QJsonValue minimum = schema.value(QStringLiteral("minimum"));
    if (minimum.isDouble() && number < minimum.toDouble()) {
        return invalid(pointer,
                       QStringLiteral("must be >= %1").arg(numberText(minimum.toDouble())));
    }
    const QJsonValue maximum = schema.value(QStringLiteral("maximum"));
    if (maximum.isDouble() && number > maximum.toDouble()) {
        return invalid(pointer,
                       QStringLiteral("must be <= %1").arg(numberText(maximum.toDouble())));
    }
    return {};
}

std::expected<QJsonValue, ValidationError>
validateValue(const QJsonObject& schema, const QJsonValue& value, const QString& pointer);

std::expected<QJsonValue, ValidationError>
validateObject(const QJsonObject& schema, const QJsonValue& value, const QString& pointer) {
    if (!value.isObject()) {
        return invalid(pointer, QStringLiteral("must be an object"));
    }
    QJsonObject object = value.toObject();
    const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();

    QStringList required;
    for (const auto& name : schema.value(QStringLiteral("required")).toArray()) {
        required.append(name.toString());
    }
    required.sort();
    for (const QString& name : std::as_const(required)) {
        if (!object.contains(name)) {
            return invalid(childPointer(pointer, name), QStringLiteral("is required"));
        }
    }

    const bool additionalAllowed =
        schema.value(QStringLiteral("additionalProperties")).toBool(true);
    const QStringList keys = object.keys(); // sorted
    for (const QString& key : keys) {
        if (!properties.contains(key)) {
            if (!additionalAllowed) {
                return invalid(childPointer(pointer, key), QStringLiteral("is not allowed"));
            }
            continue;
        }
        auto checked = validateValue(properties.value(key).toObject(), object.value(key),
                                     childPointer(pointer, key));
        if (!checked) {
            return std::unexpected(std::move(checked.error()));
        }
        object.insert(key, *checked);
    }

    for (auto it = properties.constBegin(); it != properties.constEnd(); ++it) {
        const QJsonValue defaultValue = it.value().toObject().value(QStringLiteral("default"));
        if (!object.contains(it.key()) && !defaultValue.isUndefined()) {
            object.insert(it.key(), defaultValue);
        }
    }
    return QJsonValue(object);
}

std::expected<QJsonValue, ValidationError>
validateArray(const QJsonObject& schema, const QJsonValue& value, const QString& pointer) {
    if (!value.isArray()) {
        return invalid(pointer, QStringLiteral("must be an array"));
    }
    QJsonArray array = value.toArray();
    const QJsonValue minItems = schema.value(QStringLiteral("minItems"));
    if (minItems.isDouble() && array.size() < minItems.toInteger()) {
        return invalid(pointer,
                       QStringLiteral("must have at least %1 items").arg(minItems.toInteger()));
    }
    const QJsonValue maxItems = schema.value(QStringLiteral("maxItems"));
    if (maxItems.isDouble() && array.size() > maxItems.toInteger()) {
        return invalid(pointer,
                       QStringLiteral("must have at most %1 items").arg(maxItems.toInteger()));
    }
    const QJsonObject items = schema.value(QStringLiteral("items")).toObject();
    if (!items.isEmpty()) {
        for (qsizetype i = 0; i < array.size(); ++i) {
            auto checked =
                validateValue(items, array.at(i), childPointer(pointer, QString::number(i)));
            if (!checked) {
                return std::unexpected(std::move(checked.error()));
            }
            array.replace(i, *checked);
        }
    }
    return QJsonValue(array);
}

std::expected<QJsonValue, ValidationError>
validateString(const QJsonObject& schema, const QJsonValue& value, const QString& pointer) {
    if (!value.isString()) {
        return invalid(pointer, QStringLiteral("must be a string"));
    }
    const QString text = value.toString();
    const QJsonValue minLength = schema.value(QStringLiteral("minLength"));
    if (minLength.isDouble() && text.size() < minLength.toInteger()) {
        return invalid(
            pointer,
            QStringLiteral("must be at least %1 characters long").arg(minLength.toInteger()));
    }
    const QJsonValue maxLength = schema.value(QStringLiteral("maxLength"));
    if (maxLength.isDouble() && text.size() > maxLength.toInteger()) {
        return invalid(
            pointer,
            QStringLiteral("must be at most %1 characters long").arg(maxLength.toInteger()));
    }
    const QJsonValue pattern = schema.value(QStringLiteral("pattern"));
    if (pattern.isString()) {
        const QRegularExpression regex(pattern.toString());
        if (!regex.match(text).hasMatch()) {
            return invalid(pointer,
                           QStringLiteral("must match the pattern %1").arg(pattern.toString()));
        }
    }
    if (auto checked = checkEnum(schema, value, pointer); !checked) {
        return std::unexpected(std::move(checked.error()));
    }
    return value;
}

std::expected<QJsonValue, ValidationError>
validateValue(const QJsonObject& schema, const QJsonValue& value, const QString& pointer) {
    const QString type = schema.value(typeKey).toString();
    if (type == QLatin1StringView("object")) {
        return validateObject(schema, value, pointer);
    }
    if (type == QLatin1StringView("array")) {
        return validateArray(schema, value, pointer);
    }
    if (type == QLatin1StringView("string")) {
        return validateString(schema, value, pointer);
    }
    if (type == QLatin1StringView("boolean")) {
        if (!value.isBool()) {
            return invalid(pointer, QStringLiteral("must be a boolean"));
        }
        return value;
    }
    if (type == QLatin1StringView("integer") || type == QLatin1StringView("number")) {
        const bool integer = type == QLatin1StringView("integer");
        if (integer ? !isInteger(value) : !(value.isDouble() && std::isfinite(value.toDouble()))) {
            return invalid(pointer, integer ? QStringLiteral("must be an integer")
                                            : QStringLiteral("must be a number"));
        }
        if (auto checked = checkRange(schema, value.toDouble(), pointer); !checked) {
            return std::unexpected(std::move(checked.error()));
        }
        if (auto checked = checkEnum(schema, value, pointer); !checked) {
            return std::unexpected(std::move(checked.error()));
        }
        return value;
    }
    // checkSchemaSubset() rejects other types at registration; accept anything here.
    return value;
}

std::expected<void, QString> checkSubset(const QJsonObject& schema, const QString& where,
                                         int depth) {
    static const QSet<QString> allowedKeys{QStringLiteral("type"),
                                           QStringLiteral("description"),
                                           QStringLiteral("title"),
                                           QStringLiteral("default"),
                                           QStringLiteral("properties"),
                                           QStringLiteral("required"),
                                           QStringLiteral("additionalProperties"),
                                           QStringLiteral("enum"),
                                           QStringLiteral("minimum"),
                                           QStringLiteral("maximum"),
                                           QStringLiteral("minLength"),
                                           QStringLiteral("maxLength"),
                                           QStringLiteral("pattern"),
                                           QStringLiteral("items"),
                                           QStringLiteral("minItems"),
                                           QStringLiteral("maxItems"),
                                           QStringLiteral("$schema")};
    static const QSet<QString> types{QStringLiteral("object"),  QStringLiteral("string"),
                                     QStringLiteral("integer"), QStringLiteral("number"),
                                     QStringLiteral("boolean"), QStringLiteral("array")};
    if (depth > maxSchemaDepth) {
        return std::unexpected(QStringLiteral("%1: nested too deeply").arg(where));
    }
    for (auto it = schema.constBegin(); it != schema.constEnd(); ++it) {
        if (!allowedKeys.contains(it.key())) {
            return std::unexpected(
                QStringLiteral("%1: unsupported keyword %2").arg(where, it.key()));
        }
    }
    const QString type = schema.value(typeKey).toString();
    if (!types.contains(type)) {
        return std::unexpected(QStringLiteral("%1: missing or unsupported type").arg(where));
    }
    if (schema.contains(QStringLiteral("pattern")) &&
        !QRegularExpression(schema.value(QStringLiteral("pattern")).toString()).isValid()) {
        return std::unexpected(QStringLiteral("%1: invalid pattern").arg(where));
    }
    if (type == QLatin1StringView("object")) {
        if (schema.value(QStringLiteral("additionalProperties")) != QJsonValue(false)) {
            return std::unexpected(
                QStringLiteral("%1: objects need \"additionalProperties\": false").arg(where));
        }
        const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
        for (auto it = properties.constBegin(); it != properties.constEnd(); ++it) {
            if (!it.value().isObject()) {
                return std::unexpected(
                    QStringLiteral("%1: property %2 is not a schema").arg(where, it.key()));
            }
            if (auto checked =
                    checkSubset(it.value().toObject(), childPointer(where, it.key()), depth + 1);
                !checked) {
                return checked;
            }
        }
        for (const auto& name : schema.value(QStringLiteral("required")).toArray()) {
            if (!properties.contains(name.toString())) {
                return std::unexpected(QStringLiteral("%1: required property %2 is not defined")
                                           .arg(where, name.toString()));
            }
        }
    }
    if (type == QLatin1StringView("array") && schema.contains(QStringLiteral("items"))) {
        return checkSubset(schema.value(QStringLiteral("items")).toObject(),
                           childPointer(where, QStringLiteral("items")), depth + 1);
    }
    return {};
}

} // namespace

std::optional<std::uint32_t> parseAddress(QStringView text) {
    static const QRegularExpression regex(QString::fromLatin1(addressPattern));
    if (!regex.matchView(text).hasMatch()) {
        return std::nullopt;
    }
    bool ok = false;
    const bool hex = text.startsWith(QStringLiteral("0x"));
    const qulonglong value = hex ? text.mid(2).toULongLong(&ok, 16) : text.toULongLong(&ok, 10);
    if (!ok || value > 0xFFFFFFFFULL) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

QString formatAddress(std::uint32_t address) {
    return QStringLiteral("0x") +
           QString::number(address, 16).toUpper().rightJustified(8, QLatin1Char('0'));
}

Schema Schema::object(const QString& description) {
    Schema schema(QJsonObject{{typeKey, QStringLiteral("object")},
                              {QStringLiteral("properties"), QJsonObject()},
                              {QStringLiteral("additionalProperties"), false}});
    if (!description.isEmpty()) {
        schema.description(description);
    }
    return schema;
}

Schema Schema::string(const QString& description) {
    Schema schema(QJsonObject{{typeKey, QStringLiteral("string")}});
    if (!description.isEmpty()) {
        schema.description(description);
    }
    return schema;
}

Schema Schema::integer(const QString& description) {
    Schema schema(QJsonObject{{typeKey, QStringLiteral("integer")}});
    if (!description.isEmpty()) {
        schema.description(description);
    }
    return schema;
}

Schema Schema::number(const QString& description) {
    Schema schema(QJsonObject{{typeKey, QStringLiteral("number")}});
    if (!description.isEmpty()) {
        schema.description(description);
    }
    return schema;
}

Schema Schema::boolean(const QString& description) {
    Schema schema(QJsonObject{{typeKey, QStringLiteral("boolean")}});
    if (!description.isEmpty()) {
        schema.description(description);
    }
    return schema;
}

Schema Schema::array(const Schema& items, const QString& description) {
    Schema schema(
        QJsonObject{{typeKey, QStringLiteral("array")}, {QStringLiteral("items"), items.json()}});
    if (!description.isEmpty()) {
        schema.description(description);
    }
    return schema;
}

Schema Schema::address(const QString& description) {
    Schema schema = string(description);
    schema.pattern(QString::fromLatin1(addressPattern));
    return schema;
}

Schema& Schema::description(const QString& text) {
    m_json.insert(QStringLiteral("description"), text);
    return *this;
}

Schema& Schema::minimum(double value) {
    m_json.insert(QStringLiteral("minimum"), value);
    return *this;
}

Schema& Schema::maximum(double value) {
    m_json.insert(QStringLiteral("maximum"), value);
    return *this;
}

Schema& Schema::minLength(int value) {
    m_json.insert(QStringLiteral("minLength"), value);
    return *this;
}

Schema& Schema::maxLength(int value) {
    m_json.insert(QStringLiteral("maxLength"), value);
    return *this;
}

Schema& Schema::pattern(const QString& regex) {
    m_json.insert(QStringLiteral("pattern"), regex);
    return *this;
}

Schema& Schema::enumValues(const QStringList& values) {
    m_json.insert(QStringLiteral("enum"), QJsonArray::fromStringList(values));
    return *this;
}

Schema& Schema::defaultValue(const QJsonValue& value) {
    m_json.insert(QStringLiteral("default"), value);
    return *this;
}

Schema& Schema::minItems(int value) {
    m_json.insert(QStringLiteral("minItems"), value);
    return *this;
}

Schema& Schema::maxItems(int value) {
    m_json.insert(QStringLiteral("maxItems"), value);
    return *this;
}

Schema& Schema::property(const QString& name, const Schema& schema, Required required) {
    QJsonObject properties = m_json.value(QStringLiteral("properties")).toObject();
    properties.insert(name, schema.json());
    m_json.insert(QStringLiteral("properties"), properties);
    if (required == Required::Yes) {
        QJsonArray names = m_json.value(QStringLiteral("required")).toArray();
        names.append(name);
        m_json.insert(QStringLiteral("required"), names);
    }
    return *this;
}

std::expected<QJsonObject, ValidationError> validateArguments(const QJsonObject& schema,
                                                              const QJsonObject& arguments,
                                                              const QString& pointerPrefix) {
    auto checked = validateObject(schema, QJsonValue(arguments), pointerPrefix);
    if (!checked) {
        return std::unexpected(std::move(checked.error()));
    }
    return checked->toObject();
}

std::expected<void, QString> checkSchemaSubset(const QJsonObject& schema) {
    if (schema.value(typeKey).toString() != QLatin1StringView("object")) {
        return std::unexpected(QStringLiteral("the input schema must be an object schema"));
    }
    return checkSubset(schema, QString(), 0);
}

} // namespace hio::mcp
