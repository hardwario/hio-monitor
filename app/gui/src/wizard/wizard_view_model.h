#pragma once

// WizardViewModel - the one open wizard (plan/features/0007, ADR-0066, ADR-0067), exposed as
// AppViewModel.wizard to the WizardDialog. Generic: it holds the Wizard object and turns its
// current step, buttons and checks into properties; the step machine itself is the Wizard.
//
// Cancel: while the wizard runs an operation (busyCancelText non-empty) cancel() asks first
// (cancelConfirmationNeeded); cancel(true) cancels the operation and the dialog closes when the
// wizard is no longer busy. A wizard bound to a tab is closed (its operation cancelled) when the
// tab closes: AppViewModel calls tabClosing() before the tab shuts down its session (review V2);
// the tab's `destroyed` signal is only a fallback. Owned by AppViewModel (Qt parent). GUI thread
// only.

#include "wizard/check_list_model.h"
#include "wizard/wizard.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration>

namespace hio::gui {

class DeviceTabViewModel;
class WizardRegistry;

class WizardViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppViewModel")

    Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
    Q_PROPERTY(QString wizardId READ wizardId NOTIFY openChanged)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    // [{id, title, state: "done" | "current" | "pending"}] of the steps shown in the step list.
    Q_PROPERTY(QVariantList steps READ steps NOTIFY changed)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY changed) // in `steps`, -1 when hidden
    Q_PROPERTY(QVariantMap step READ step NOTIFY changed)         // WizardStep::toMap()
    Q_PROPERTY(QString stepLabel READ stepLabel NOTIFY changed)   // "Step 2 of 4 · Cable"
    Q_PROPERTY(
        QString stepAnnouncement READ stepAnnouncement NOTIFY changed) // "Step 2 of 4, Cable"
    Q_PROPERTY(bool canBack READ canBack NOTIFY changed)
    Q_PROPERTY(bool canPrimary READ canPrimary NOTIFY changed)
    Q_PROPERTY(QString busyCancelText READ busyCancelText NOTIFY changed)
    Q_PROPERTY(hio::gui::CheckListModel* checks READ checks NOTIFY changed)
    Q_PROPERTY(QObject* embedded READ embedded NOTIFY changed)
    Q_PROPERTY(QUrl embeddedSource READ embeddedSource NOTIFY changed)
    Q_PROPERTY(bool closing READ closing NOTIFY changed) // waiting for a cancelled operation

public:
    explicit WizardViewModel(const WizardRegistry& registry, QObject* parent = nullptr);
    ~WizardViewModel() override;
    WizardViewModel(const WizardViewModel&) = delete;
    WizardViewModel& operator=(const WizardViewModel&) = delete;
    WizardViewModel(WizardViewModel&&) = delete;
    WizardViewModel& operator=(WizardViewModel&&) = delete;

    // Shows `wizard` (this object becomes its Qt parent) and calls Wizard::start. False (the
    // wizard is deleted) when a wizard is open already (ADR-0066: one at a time).
    bool start(Wizard* wizard, const ErrorInfo& context, DeviceTabViewModel* tab);
    // `tab` is about to close (AppViewModel::removeTab, before DeviceTabViewModel::shutdown()):
    // when the open wizard works on it, its operation is cancelled (Wizard::cancelOperation) and
    // the dialog closes while the tab and its session still exist. Other tabs: nothing happens.
    void tabClosing(const DeviceTabViewModel* tab);

    [[nodiscard]] bool isOpen() const { return !m_wizard.isNull(); }
    [[nodiscard]] QString wizardId() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] QVariantList steps() const;
    [[nodiscard]] int currentIndex() const;
    [[nodiscard]] QVariantMap step() const;
    [[nodiscard]] QString stepLabel() const;
    [[nodiscard]] QString stepAnnouncement() const;
    [[nodiscard]] bool canBack() const;
    [[nodiscard]] bool canPrimary() const;
    [[nodiscard]] QString busyCancelText() const;
    [[nodiscard]] CheckListModel* checks() const;
    [[nodiscard]] QObject* embedded() const;
    [[nodiscard]] QUrl embeddedSource() const;
    [[nodiscard]] bool closing() const noexcept { return m_closing; }
    [[nodiscard]] Wizard* wizard() const { return m_wizard.data(); }
    [[nodiscard]] DeviceTabViewModel* tab() const;

    Q_INVOKABLE void primary();
    Q_INVOKABLE void back();
    Q_INVOKABLE void choose(const QString& choiceId);
    Q_INVOKABLE void action(const QString& actionId);
    // Esc / ×. While busy and !force: cancelConfirmationNeeded(busyCancelText).
    Q_INVOKABLE void cancel(bool force = false);
    Q_INVOKABLE void copyText(const QString& text); // CommandBlock [Copy]

    // ADR-0076: the image of an illustration slot: <slot>-dark.svg in the dark theme when it
    // exists, else <slot>.svg, else the placeholder (also for an empty slot id).
    Q_INVOKABLE [[nodiscard]] static QUrl illustrationUrl(const QString& slot, bool dark);
    // The slots of plan 0007's table (shared by 0008-0010; 0008 adds setup-welcome).
    [[nodiscard]] static QStringList illustrationSlots();
    // True when the slot has its own image (not the placeholder fallback).
    [[nodiscard]] static bool hasIllustration(const QString& slot);

signals:
    void openChanged();
    void changed();
    void cancelConfirmationNeeded(const QString& text);
    // The open wizard should come to the front (another wizard was asked for).
    void raiseRequested();
    void handOff(const QString& action, hio::gui::DeviceTabViewModel* tab);
    void copied();

private:
    void close(bool completed);
    void onWizardChanged();

    const WizardRegistry& m_registry;
    QPointer<Wizard> m_wizard; // child while open
    QPointer<DeviceTabViewModel> m_tab;
    bool m_tabBound = false;
    bool m_closing = false;
};

} // namespace hio::gui
