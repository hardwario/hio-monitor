#include "viewmodels/error_info.h"

#include "hio/core/error.h"
#include "hio/core/json.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringLiteral>

namespace hio::gui {

namespace error_action {

Value forCategory(ErrorCategory category) noexcept {
    switch (category) {
    case ErrorCategory::Probe:
    case ErrorCategory::Target:
        return Reconnect;
    case ErrorCategory::Firmware:
        return ChooseFile;
    case ErrorCategory::Dependency:
        return OpenSettings;
    case ErrorCategory::Io:
    case ErrorCategory::Network:
    case ErrorCategory::Timeout:
    case ErrorCategory::Internal:
        return Retry;
    case ErrorCategory::Usage:
    case ErrorCategory::Cancelled:
        return Dismiss;
    }
    return Dismiss;
}

} // namespace error_action

ErrorInfo::ErrorInfo(const Error& error)
    : m_valid(true), m_code(toQString(idOf(error.code))),
      m_category(toQString(categoryId(categoryOf(error.code)))),
      m_message(toQString(error.message)), m_cause(toQString(error.cause)),
      m_suggestion(toQString(error.suggestion)), m_detail(toQString(error.detail)),
      // The kebab-case id comes from the JSON form, the single place that names wizards.
      m_wizard(toJson(error).value(QStringLiteral("wizard")).toString()),
      m_primaryAction(error_action::forCategory(categoryOf(error.code))) {}

QString ErrorInfo::detailsText() const {
    if (m_detail.isEmpty()) {
        return m_code;
    }
    return m_code + QLatin1Char('\n') + m_detail;
}

} // namespace hio::gui
