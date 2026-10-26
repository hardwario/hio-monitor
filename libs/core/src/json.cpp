#include "hio/core/json.h"

#include "hio/core/error.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QString>
#include <QStringLiteral>
#include <QtTypes>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace hio {

namespace {

// ADR-0026: bump only for breaking changes of the envelope.
constexpr int envelopeSchema = 1;

QString latin1(std::string_view text) {
    return QLatin1StringView(text.data(), static_cast<qsizetype>(text.size())).toString();
}

} // namespace

std::string_view wizardIdString(WizardId wizard) noexcept {
    switch (wizard) {
    case WizardId::FirstRunSetup:
        return "first-run-setup";
    case WizardId::ConnectDevice:
        return "connect-device";
    case WizardId::FlashFirmware:
        return "flash-firmware";
    case WizardId::Troubleshooting:
        return "troubleshooting";
    case WizardId::BlePairing:
        return "ble-pairing";
    }
    return "troubleshooting";
}

std::optional<WizardId> wizardIdFromString(std::string_view id) noexcept {
    for (const WizardId wizard :
         {WizardId::FirstRunSetup, WizardId::ConnectDevice, WizardId::FlashFirmware,
          WizardId::Troubleshooting, WizardId::BlePairing}) {
        if (wizardIdString(wizard) == id) {
            return wizard;
        }
    }
    return std::nullopt;
}

QJsonObject toJson(const Error& error) {
    QJsonObject object;
    object.insert(QStringLiteral("code"), latin1(idOf(error.code)));
    object.insert(QStringLiteral("category"), latin1(categoryId(categoryOf(error.code))));
    object.insert(QStringLiteral("message"), toQString(error.message));
    object.insert(QStringLiteral("cause"), toQString(error.cause));
    object.insert(QStringLiteral("suggestion"), toQString(error.suggestion));
    object.insert(QStringLiteral("detail"), toQString(error.detail));
    object.insert(QStringLiteral("wizard"), error.wizard.has_value()
                                                ? QJsonValue(latin1(wizardIdString(*error.wizard)))
                                                : QJsonValue(QJsonValue::Null));
    return object;
}

QJsonObject okEnvelope(const QJsonValue& data) {
    QJsonObject object;
    object.insert(QStringLiteral("schema"), envelopeSchema);
    object.insert(QStringLiteral("ok"), true);
    object.insert(QStringLiteral("data"), data);
    return object;
}

QJsonObject errorEnvelope(const Error& error) {
    QJsonObject object;
    object.insert(QStringLiteral("schema"), envelopeSchema);
    object.insert(QStringLiteral("ok"), false);
    object.insert(QStringLiteral("error"), toJson(error));
    return object;
}

QJsonObject errorEnvelope(const Error& error, const QJsonValue& data) {
    QJsonObject object = errorEnvelope(error);
    object.insert(QStringLiteral("data"), data);
    return object;
}

std::string toCompactUtf8(const QJsonObject& object) {
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

QString toQString(std::string_view utf8) {
    return QString::fromUtf8(QByteArrayView(utf8.data(), static_cast<qsizetype>(utf8.size())));
}

std::string toStdString(const QString& text) { return text.toStdString(); }

std::string toUtf8(const std::filesystem::path& path) {
#ifdef _WIN32
    const std::wstring& native = path.native();
    return QString::fromWCharArray(native.data(), static_cast<qsizetype>(native.size()))
        .toStdString();
#else
    return path.native();
#endif
}

} // namespace hio
