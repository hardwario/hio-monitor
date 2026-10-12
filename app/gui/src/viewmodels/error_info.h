#pragma once

// ErrorInfo - hio::Error as a QML value type (plan/features/0004-m3-gui-shell.md, A4).
//
// Every backend error is shown inline in an ErrorPanel (UX 0003): message, "Probably:" cause,
// "What to do:" suggestion, a primary action, the "Help me fix this" button when the error has a
// wizard (disabled until M4) and a Details expander with the code and the technical detail.
// The primary action follows the error category (error_action::forCategory); a page may override it
// in QML (ErrorPanel.primaryAction).
//
// Texts are already translated by the backend when the error was created (ADR-0027); an error on
// screen keeps its language after a live language switch.

#include "hio/core/error.h"

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QtQmlIntegration>
#include <QtTypes>

#include <cstdint>

namespace hio::gui {

// The primary action of an ErrorPanel (QML: ErrorAction.Reconnect, ...).
namespace error_action {
Q_NAMESPACE
QML_NAMED_ELEMENT(ErrorAction)

// Unscoped: QML and the ErrorInfo property use the values as ints.
enum Value : quint8 { // NOLINT(cppcoreguidelines-use-enum-class)
    Reconnect,        // Probe, Target
    ChooseFile,       // Firmware ("Choose another file")
    OpenSettings,     // Dependency
    Retry,            // Io, Network, Timeout, Internal
    Dismiss,          // Usage, Cancelled
};
Q_ENUM_NS(Value)

[[nodiscard]] Value forCategory(ErrorCategory category) noexcept;

} // namespace error_action

class ErrorInfo {
    Q_GADGET
    QML_VALUE_TYPE(errorInfo)

    Q_PROPERTY(bool valid READ valid CONSTANT)
    Q_PROPERTY(QString code READ code CONSTANT)
    Q_PROPERTY(QString category READ category CONSTANT)
    Q_PROPERTY(QString message READ message CONSTANT)
    Q_PROPERTY(QString cause READ cause CONSTANT)
    Q_PROPERTY(QString suggestion READ suggestion CONSTANT)
    Q_PROPERTY(QString detail READ detail CONSTANT)
    Q_PROPERTY(QString wizard READ wizard CONSTANT)
    Q_PROPERTY(int primaryAction READ primaryAction CONSTANT)

public:
    ErrorInfo() = default; // invalid: no error
    explicit ErrorInfo(const Error& error);

    [[nodiscard]] bool valid() const noexcept { return m_valid; }
    [[nodiscard]] QString code() const { return m_code; }
    [[nodiscard]] QString category() const { return m_category; }
    [[nodiscard]] QString message() const { return m_message; }
    [[nodiscard]] QString cause() const { return m_cause; }
    [[nodiscard]] QString suggestion() const { return m_suggestion; }
    [[nodiscard]] QString detail() const { return m_detail; }
    // The kebab-case wizard id ("connect-device") or "" when the error has none.
    [[nodiscard]] QString wizard() const { return m_wizard; }
    [[nodiscard]] int primaryAction() const noexcept { return m_primaryAction; }

    // The text for the Details expander and its Copy button: "<code>\n<detail>".
    Q_INVOKABLE [[nodiscard]] QString detailsText() const;

    [[nodiscard]] bool operator==(const ErrorInfo& other) const = default;

private:
    bool m_valid = false;
    QString m_code;
    QString m_category;
    QString m_message;
    QString m_cause;
    QString m_suggestion;
    QString m_detail;
    QString m_wizard;
    int m_primaryAction = error_action::Dismiss;
};

} // namespace hio::gui

Q_DECLARE_METATYPE(hio::gui::ErrorInfo)
