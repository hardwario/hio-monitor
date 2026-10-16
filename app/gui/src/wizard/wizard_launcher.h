#pragma once

// WizardLauncher - opens wizards (plan/features/0007): "Help me fix this" in an ErrorPanel (the
// error's kebab wizard id + the error as context), the Help menu and
// DeviceTabViewModel::openWizard. A wizard whose id is not registered cannot be opened (canOpen =
// false: the UI hides the button). When a wizard is open already it is brought to the front instead
// (ADR-0066). Owned by AppViewModel (Qt parent). GUI thread only.

#include "viewmodels/error_info.h"

#include <QObject>
#include <QString>
#include <QtQmlIntegration>

#include <functional>

namespace hio::gui {

class DeviceTabViewModel;
class WizardRegistry;
class WizardViewModel;

class WizardLauncher : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppViewModel")

public:
    // `tabAt(-1)` is the current tab; nullptr when there is none.
    WizardLauncher(const WizardRegistry& registry, WizardViewModel& wizard,
                   std::function<DeviceTabViewModel*(int tabIndex)> tabAt,
                   QObject* parent = nullptr);

    // Registered? (`wizardId` is the kebab id, e.g. "connect-device"; "" = false.)
    Q_INVOKABLE [[nodiscard]] bool canOpen(const QString& wizardId) const;
    // Opens the wizard with the error as context on the tab (-1 = the current tab; ignored for
    // global wizards). False when it is not registered or another wizard is open (raised).
    Q_INVOKABLE bool open(const QString& wizardId, const hio::gui::ErrorInfo& context,
                          int tabIndex = -1);
    // The same without an error (Help menu).
    Q_INVOKABLE bool start(const QString& wizardId, int tabIndex = -1);
    // For DeviceTabViewModel::openWizard.
    bool openForTab(const QString& wizardId, DeviceTabViewModel* tab);

private:
    bool launch(const QString& wizardId, const ErrorInfo& context, DeviceTabViewModel* tab);

    const WizardRegistry& m_registry;
    WizardViewModel& m_wizard;
    std::function<DeviceTabViewModel*(int)> m_tabAt;
};

} // namespace hio::gui
