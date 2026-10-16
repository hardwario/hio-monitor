#include "wizard/wizard_launcher.h"

#include "error_info.h"
#include "gui_log.h"
#include "wizard.h"
#include "wizard/wizard_registry.h"
#include "wizard/wizard_view_model.h"

#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QtCore/qtmetamacros.h>

#include <functional>
#include <utility>

namespace hio::gui {

WizardLauncher::WizardLauncher(const WizardRegistry& registry, WizardViewModel& wizard,
                               std::function<DeviceTabViewModel*(int)> tabAt, QObject* parent)
    : QObject(parent), m_registry(registry), m_wizard(wizard), m_tabAt(std::move(tabAt)) {}

bool WizardLauncher::canOpen(const QString& wizardId) const {
    return !wizardId.isEmpty() && m_registry.has(wizardId);
}

bool WizardLauncher::open(const QString& wizardId, const ErrorInfo& context, int tabIndex) {
    return launch(wizardId, context, m_tabAt ? m_tabAt(tabIndex) : nullptr);
}

bool WizardLauncher::start(const QString& wizardId, int tabIndex) {
    return open(wizardId, ErrorInfo(), tabIndex);
}

bool WizardLauncher::openForTab(const QString& wizardId, DeviceTabViewModel* tab) {
    return launch(wizardId, ErrorInfo(), tab);
}

bool WizardLauncher::launch(const QString& wizardId, const ErrorInfo& context,
                            DeviceTabViewModel* tab) {
    const WizardDescriptor* descriptor = m_registry.find(wizardId);
    if (descriptor == nullptr) {
        qCInfo(lcGui).noquote() << "Wizard" << wizardId << "is not registered";
        return false;
    }
    if (m_wizard.isOpen()) {
        emit m_wizard.raiseRequested(); // ADR-0066: one at a time
        return false;
    }
    if (descriptor->perTab && tab == nullptr) {
        qCInfo(lcGui).noquote() << "Wizard" << wizardId << "needs a tab";
        return false;
    }
    // Ownership: the factory creates the wizard with the WizardViewModel as Qt parent.
    Wizard* wizard = descriptor->create(&m_wizard);
    return m_wizard.start(wizard, context, descriptor->perTab ? tab : nullptr);
}

} // namespace hio::gui
