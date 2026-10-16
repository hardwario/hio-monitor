#pragma once

// The wizard contract (plan/features/0007 § GUI wizard contract, ADR-0066, ADR-0067). Wizards
// 0008-0010 implement it: one Wizard subclass per wizard, registered with a WizardDescriptor in the
// WizardRegistry. A Wizard is a C++ step machine; QML renders the current step by its kind with a
// fixed component (WizardDialog). Texts are translated in C++ (ADR-0027); no logic in QML.
//
// Ownership: a Wizard is created by its descriptor's factory with the WizardViewModel as Qt parent
// and deleted (deleteLater) when the dialog closes. GUI thread only.

#include "hio/core/error.h"
#include "viewmodels/error_info.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration>

#include <functional>
#include <vector>

namespace hio::gui {

class CheckListModel;
class DeviceTabViewModel;

// The kind of a step (QML: StepKind.Info, ...): each is rendered by a fixed component.
namespace wizard_step {
Q_NAMESPACE
QML_NAMED_ELEMENT(StepKind)

// Unscoped: QML compares the int `step.kind` with StepKind.Info etc.
enum Kind : quint8 { // NOLINT(cppcoreguidelines-use-enum-class)
    Info,            // illustration (optional), heading, body, optional link
    Checks,          // CheckList with [Check again]
    Choice,          // radio cards
    Embedded,        // a named control (probe picker, firmware source, phase progress)
    Commands,        // monospace command lines, each with [Copy]
    Done,            // success mark, summary, hand-off buttons
};
Q_ENUM_NS(Kind)
} // namespace wizard_step

using StepKind = wizard_step::Kind;

// One step as a value; rebuilt by the wizard when it changes.
struct WizardStep {
    QString id; // "cable", "power", ...
    StepKind kind = StepKind::Info;
    QString title;        // short, shown in the step list (translated)
    QString heading;      // translated
    QString body;         // translated; simple rich text (<b>, <br>) allowed
    QString illustration; // slot id (ADR-0076) or ""
    QString embedded;     // Embedded: "probe-picker" | "firmware-source" | "phase-progress"
    QStringList commands; // Commands
    QVariantList choices; // Choice: [{id, title, subtitle, selected}]
    QString primaryText;  // the default button ("Next", "Check again", "Flash", "Finish")
    bool showInStepList = true;
    // Additions to the plan's sketch (documented in plan 0007 Implementation notes):
    QString linkText; // Info: an optional link below the body
    QString linkUrl;
    QVariantList actions; // Done (and others): extra buttons [{id, text}] -> Wizard::action

    // The step for QML (keys as the members; `kind` as int).
    [[nodiscard]] QVariantMap toMap() const;
};

class Wizard : public QObject {
    Q_OBJECT

public:
    explicit Wizard(QObject* parent) : QObject(parent) {}

    [[nodiscard]] virtual QString id() const = 0;    // kebab-case (ADR-0067)
    [[nodiscard]] virtual QString title() const = 0; // translated
    // Called once; `context` = the error that launched the wizard (may be invalid), `tab` = the
    // tab it works on (nullptr for global wizards).
    //
    // The tab is not owned and may close while the wizard is open. A wizard keeps it only as
    // QPointer<DeviceTabViewModel> (never a raw pointer or reference) and checks it before every
    // use. When the tab closes, WizardViewModel calls cancelOperation() and closes the dialog
    // before the tab shuts down its session (review V2); the wizard must not start anything on
    // the tab after that. The test wizard (app/gui/tests/support/test_wizard.*) shows the pattern.
    virtual void start(const ErrorInfo& context, DeviceTabViewModel* tab) = 0;
    [[nodiscard]] virtual std::vector<WizardStep> plannedSteps() const = 0; // for the step list
    [[nodiscard]] virtual WizardStep current() const = 0;
    virtual void primary() = 0; // the default button
    virtual void back() = 0;
    virtual void choose(const QString& choiceId) { (void)choiceId; } // Choice steps
    [[nodiscard]] virtual bool canBack() const = 0;
    [[nodiscard]] virtual bool canPrimary() const = 0;
    // Non-empty while an operation of the wizard runs: the question of the Cancel confirmation.
    [[nodiscard]] virtual QString busyCancelText() const { return {}; }
    virtual void cancelOperation() {}
    [[nodiscard]] virtual CheckListModel* checks() { return nullptr; }     // Checks steps
    [[nodiscard]] virtual QObject* embeddedViewModel() { return nullptr; } // Embedded steps
    // A button of WizardStep::actions; the default hands the action off to the application.
    virtual void action(const QString& actionId) { emit handOff(actionId); }

signals:
    void changed();                      // step, buttons or checks changed
    void finished(bool completed);       // the dialog closes
    void handOff(const QString& action); // "open-console", "open-flash-page", "open-settings:<id>"
};

struct WizardDescriptor {
    WizardId id = WizardId::Troubleshooting;
    bool perTab = true; // false: a global wizard (setup)
    // Creates the wizard with `parent` as Qt parent (ownership).
    std::function<Wizard*(QObject* parent)> create;
};

} // namespace hio::gui
