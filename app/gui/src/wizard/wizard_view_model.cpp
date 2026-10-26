#include "wizard/wizard_view_model.h"

#include "check_list_model.h"
#include "error_info.h"
#include "gui_log.h"
#include "viewmodels/device_tab_view_model.h"
#include "wizard.h"
#include "wizard/wizard_registry.h"

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtCore/qtmetamacros.h>

#include <vector>

namespace hio::gui {

WizardViewModel::WizardViewModel(const WizardRegistry& registry, QObject* parent)
    : QObject(parent), m_registry(registry) {}

WizardViewModel::~WizardViewModel() {
    if (!m_wizard.isNull()) {
        m_wizard->disconnect(this);
        m_wizard->cancelOperation();
    }
}

bool WizardViewModel::start(Wizard* wizard, const ErrorInfo& context, DeviceTabViewModel* tab) {
    if (wizard == nullptr) {
        return false;
    }
    if (!m_wizard.isNull()) {
        qCInfo(lcGui).noquote() << "Wizard" << wizard->id() << "refused:" << m_wizard->id()
                                << "is open";
        wizard->deleteLater();
        emit raiseRequested();
        return false;
    }
    wizard->setParent(this); // Qt parent ownership until close()
    m_wizard = wizard;
    m_tab = tab;
    m_tabBound = tab != nullptr;
    m_closing = false;
    connect(wizard, &Wizard::changed, this, &WizardViewModel::onWizardChanged);
    connect(wizard, &Wizard::finished, this, [this](bool completed) { close(completed); });
    connect(wizard, &Wizard::handOff, this,
            [this](const QString& action) { emit handOff(action, m_tab.data()); });
    if (tab != nullptr) {
        // Fallback only: AppViewModel calls tabClosing() before the tab shuts down (review V2).
        // Here the tab is being destroyed already; the wizard's QPointer to it is null.
        connect(tab, &QObject::destroyed, this, [this] {
            if (m_wizard) {
                m_wizard->cancelOperation();
            }
            close(false);
        });
    }
    qCInfo(lcGui).noquote() << "Wizard" << wizard->id() << "opened";
    wizard->start(context, tab);
    emit openChanged();
    emit changed();
    return true;
}

DeviceTabViewModel* WizardViewModel::tab() const { return m_tab.data(); }

void WizardViewModel::tabClosing(const DeviceTabViewModel* tab) {
    if (m_wizard.isNull() || tab == nullptr || m_tab.data() != tab) {
        return;
    }
    qCInfo(lcGui).noquote() << "Wizard" << m_wizard->id() << "closed with its tab";
    m_wizard->cancelOperation();
    close(false); // also when the cancelled operation has not ended yet: the tab goes away
}

QString WizardViewModel::wizardId() const {
    return !m_wizard.isNull() ? m_wizard->id() : QString();
}

QString WizardViewModel::title() const {
    return !m_wizard.isNull() ? m_wizard->title() : QString();
}

QVariantList WizardViewModel::steps() const {
    QVariantList list;
    if (m_wizard.isNull()) {
        return list;
    }
    const WizardStep current = m_wizard->current();
    const std::vector<WizardStep> planned = m_wizard->plannedSteps();
    bool afterCurrent = false;
    bool currentFound = false;
    for (const WizardStep& step : planned) {
        if (!step.showInStepList) {
            continue;
        }
        QString state = QStringLiteral("pending");
        if (step.id == current.id) {
            state = QStringLiteral("current");
            currentFound = true;
            afterCurrent = true;
        } else if (!afterCurrent) {
            state = QStringLiteral("done");
        }
        list.push_back(QVariantMap{{QStringLiteral("id"), step.id},
                                   {QStringLiteral("title"), step.title},
                                   {QStringLiteral("state"), state}});
    }
    if (!currentFound) {
        // The current step is not in the list (e.g. a hidden sub-step): nothing is "done" for sure.
        for (QVariant& item : list) {
            QVariantMap map = item.toMap();
            map[QStringLiteral("state")] = QStringLiteral("pending");
            item = map;
        }
    }
    return list;
}

int WizardViewModel::currentIndex() const {
    if (m_wizard.isNull()) {
        return -1;
    }
    const QString current = m_wizard->current().id;
    int index = 0;
    for (const WizardStep& step : m_wizard->plannedSteps()) {
        if (!step.showInStepList) {
            continue;
        }
        if (step.id == current) {
            return index;
        }
        ++index;
    }
    return -1;
}

QVariantMap WizardViewModel::step() const {
    return !m_wizard.isNull() ? m_wizard->current().toMap() : QVariantMap();
}

QString WizardViewModel::stepLabel() const {
    const int index = currentIndex();
    if (m_wizard.isNull() || index < 0) {
        return {};
    }
    return tr("Step %1 of %2 · %3")
        .arg(index + 1)
        .arg(steps().size())
        .arg(m_wizard->current().title);
}

QString WizardViewModel::stepAnnouncement() const {
    const int index = currentIndex();
    if (m_wizard.isNull()) {
        return {};
    }
    if (index < 0) {
        return m_wizard->current().heading;
    }
    return tr("Step %1 of %2, %3")
        .arg(index + 1)
        .arg(steps().size())
        .arg(m_wizard->current().title);
}

bool WizardViewModel::canBack() const {
    return !m_wizard.isNull() && busyCancelText().isEmpty() && m_wizard->canBack();
}

bool WizardViewModel::canPrimary() const {
    return !m_wizard.isNull() && !m_closing && m_wizard->canPrimary();
}

QString WizardViewModel::busyCancelText() const {
    return !m_wizard.isNull() ? m_wizard->busyCancelText() : QString();
}

CheckListModel* WizardViewModel::checks() const {
    return !m_wizard.isNull() ? m_wizard->checks() : nullptr;
}

QObject* WizardViewModel::embedded() const {
    return !m_wizard.isNull() ? m_wizard->embeddedViewModel() : nullptr;
}

QUrl WizardViewModel::embeddedSource() const {
    if (m_wizard.isNull()) {
        return {};
    }
    const WizardStep current = m_wizard->current();
    if (current.kind != StepKind::Embedded) {
        return {};
    }
    return m_registry.embedded(current.embedded);
}

void WizardViewModel::primary() {
    if (canPrimary()) {
        m_wizard->primary();
    }
}

void WizardViewModel::back() {
    if (canBack()) {
        m_wizard->back();
    }
}

void WizardViewModel::choose(const QString& choiceId) {
    if (!m_wizard.isNull()) {
        m_wizard->choose(choiceId);
    }
}

void WizardViewModel::action(const QString& actionId) {
    if (!m_wizard.isNull()) {
        m_wizard->action(actionId);
    }
}

void WizardViewModel::cancel(bool force) {
    if (m_wizard.isNull()) {
        return;
    }
    if (busyCancelText().isEmpty()) {
        close(false);
        return;
    }
    if (!force) {
        emit cancelConfirmationNeeded(busyCancelText());
        return;
    }
    // [Cancel and close]: the dialog closes when the operation has ended (onWizardChanged).
    m_closing = true;
    m_wizard->cancelOperation();
    if (!m_wizard.isNull() && busyCancelText().isEmpty()) {
        close(false);
        return;
    }
    emit changed();
}

void WizardViewModel::copyText(const QString& text) {
    if (QClipboard* clipboard = QGuiApplication::clipboard(); clipboard != nullptr) {
        clipboard->setText(text);
        emit copied();
    }
}

namespace {

QString illustrationPath(const QString& name) {
    return QStringLiteral(":/qt/qml/HardwarioMonitor/assets/wizard/%1.svg").arg(name);
}

QUrl qrcUrl(const QString& path) { return {QStringLiteral("qrc") + path}; }

} // namespace

QUrl WizardViewModel::illustrationUrl(const QString& slot, bool dark) {
    if (!slot.isEmpty()) {
        if (dark && QFile::exists(illustrationPath(slot + QStringLiteral("-dark")))) {
            return qrcUrl(illustrationPath(slot + QStringLiteral("-dark")));
        }
        if (QFile::exists(illustrationPath(slot))) {
            return qrcUrl(illustrationPath(slot));
        }
    }
    return qrcUrl(illustrationPath(QStringLiteral("placeholder")));
}

QStringList WizardViewModel::illustrationSlots() {
    return {QStringLiteral("jlink-usb"), QStringLiteral("chester-app-cable"),
            QStringLiteral("chester-lte-cable"), QStringLiteral("chester-power"),
            QStringLiteral("placeholder")};
}

bool WizardViewModel::hasIllustration(const QString& slot) {
    return !slot.isEmpty() && QFile::exists(illustrationPath(slot));
}

void WizardViewModel::onWizardChanged() {
    if (m_closing && !m_wizard.isNull() && busyCancelText().isEmpty()) {
        close(false);
        return;
    }
    emit changed();
}

void WizardViewModel::close(bool completed) {
    if (m_wizard.isNull()) {
        return;
    }
    qCInfo(lcGui).noquote() << "Wizard" << m_wizard->id() << (completed ? "completed" : "closed");
    Wizard* wizard = m_wizard.data();
    wizard->disconnect(this);
    if (!m_tab.isNull()) {
        m_tab->disconnect(this);
    }
    m_wizard.clear();
    m_tab.clear();
    m_tabBound = false;
    m_closing = false;
    wizard->deleteLater(); // never inside its own signal
    emit openChanged();
    emit changed();
}

} // namespace hio::gui
