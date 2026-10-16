#include "support/test_wizard.h"

#include "hio/core/error.h"
#include "hio/diag/check.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "wizard/check_list_model.h"
#include "wizard/wizard.h"
#include "wizard/wizard_registry.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtCore/qtmetamacros.h>
#include <QtTypes>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace hio::gui::test {

namespace {

const std::array<QString, 6> stepIds{QStringLiteral("info"),     QStringLiteral("choice"),
                                     QStringLiteral("checks"),   QStringLiteral("commands"),
                                     QStringLiteral("embedded"), QStringLiteral("done")};

} // namespace

TestWizard::TestWizard(QObject* parent)
    : Wizard(parent),
      // Qt parent ownership: the check list and the embedded view model are children.
      m_checks(new CheckListModel(this)),        // NOLINT(cppcoreguidelines-owning-memory)
      m_embedded(new TestWizardEmbedded(this)) { // NOLINT(cppcoreguidelines-owning-memory)
    const std::array ids{QStringLiteral("usb-access"), QStringLiteral("probes")};
    const std::array titles{QStringLiteral("USB access to J-Link"),
                            QStringLiteral("J-Link probes")};
    m_checks->reset(ids, titles);
}

void TestWizard::start(const ErrorInfo& context, DeviceTabViewModel* tab) {
    m_context = context;
    m_tab = tab;
    // A launch from an error starts at the step that explains it.
    if (context.valid()) {
        goTo(2);
    }
}

std::vector<WizardStep> TestWizard::plannedSteps() const {
    std::vector<WizardStep> steps;
    const QStringList titles{QStringLiteral("Probe"),    QStringLiteral("Device"),
                             QStringLiteral("Check"),    QStringLiteral("Commands"),
                             QStringLiteral("Embedded"), QStringLiteral("Done")};
    for (qsizetype i = 0; i < titles.size(); ++i) {
        WizardStep step;
        step.id = stepIds.at(static_cast<std::size_t>(i));
        step.title = titles.at(i);
        steps.push_back(step);
    }
    return steps;
}

WizardStep TestWizard::current() const {
    WizardStep step = plannedSteps().at(static_cast<std::size_t>(m_index));
    switch (m_index) {
    case 0:
        step.kind = StepKind::Info;
        step.illustration = QStringLiteral("jlink-usb");
        step.heading = QStringLiteral("Connect the J-Link");
        step.body = QStringLiteral("Connect the J-Link to your computer with a <b>USB cable</b>.");
        step.linkText = QStringLiteral("Documentation");
        step.linkUrl = QStringLiteral("https://docs.hardwario.com/");
        step.primaryText = QStringLiteral("Next");
        break;
    case 1:
        step.kind = StepKind::Choice;
        step.heading = QStringLiteral("Which MCU do you want to connect?");
        step.choices = QVariantList{
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("app")},
                {QStringLiteral("title"), QStringLiteral("Application MCU (app)")},
                {QStringLiteral("subtitle"), QStringLiteral("Console and application firmware")},
                {QStringLiteral("selected"), m_choice == QStringLiteral("app")}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("lte")},
                        {QStringLiteral("title"), QStringLiteral("LTE MCU (lte)")},
                        {QStringLiteral("subtitle"), QStringLiteral("LTE firmware")},
                        {QStringLiteral("selected"), m_choice == QStringLiteral("lte")}}};
        step.primaryText = QStringLiteral("Next");
        break;
    case 2:
        step.kind = StepKind::Checks;
        step.heading = QStringLiteral("Checking the connection");
        step.primaryText =
            m_checks->allPassed() ? QStringLiteral("Next") : QStringLiteral("Check again");
        break;
    case 3:
        step.kind = StepKind::Commands;
        step.heading = QStringLiteral("Allow access to the J-Link");
        step.body = QStringLiteral("Run these commands in a terminal, then reconnect the J-Link.");
        step.commands = QStringList{QStringLiteral("echo 'rule' | sudo tee /tmp/x.rules"),
                                    QStringLiteral("sudo udevadm trigger")};
        step.primaryText = QStringLiteral("Next");
        break;
    case 4:
        step.kind = StepKind::Embedded;
        step.embedded = QStringLiteral("test-embedded");
        step.heading = QStringLiteral("Embedded control");
        step.primaryText = QStringLiteral("Flash");
        break;
    default:
        step.kind = StepKind::Done;
        step.heading = QStringLiteral("Connected");
        step.body = QStringLiteral("The device is connected.");
        step.primaryText = QStringLiteral("Finish");
        step.actions =
            QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("open-console")},
                                     {QStringLiteral("text"), QStringLiteral("Open console")}}};
        break;
    }
    return step;
}

void TestWizard::showResults() {
    m_checks->update(diag::CheckResult{.id = "usb-access",
                                       .status = diag::CheckStatus::Ok,
                                       .title = {},
                                       .result = "Access granted",
                                       .error = std::nullopt});
    Error error = makeError(ErrorCode::ProbeNotFound);
    m_checks->update(diag::CheckResult{.id = "probes",
                                       .status = diag::CheckStatus::Failed,
                                       .title = {},
                                       .result = error.message,
                                       .error = error});
}

void TestWizard::primary() {
    if (m_busy) {
        return;
    }
    if (m_index == 2 && !m_checks->allPassed()) {
        if (m_checks->status(QStringLiteral("probes")) == diag::CheckStatus::Pending) {
            showResults();
        } else {
            // "Check again": now everything passes.
            m_checks->update(diag::CheckResult{.id = "probes",
                                               .status = diag::CheckStatus::Ok,
                                               .title = {},
                                               .result = "1 found: 801000001",
                                               .error = std::nullopt});
        }
        emit changed();
        return;
    }
    if (m_index == 4) {
        startOperation();
        return;
    }
    if (m_index == static_cast<int>(stepIds.size()) - 1) {
        emit finished(true);
        return;
    }
    goTo(m_index + 1);
}

void TestWizard::back() {
    if (canBack()) {
        goTo(m_index - 1);
    }
}

void TestWizard::choose(const QString& choiceId) {
    m_choice = choiceId;
    emit changed();
}

bool TestWizard::canBack() const { return m_index > 0 && !m_busy; }

bool TestWizard::canPrimary() const { return !m_busy; }

QString TestWizard::busyCancelText() const { return m_busy ? busyText : QString(); }

void TestWizard::cancelOperation() {
    ++m_cancelCalls;
    m_tabSessionAtCancel = m_tab && m_tab->session() != nullptr; // QPointer: checked first
    if (m_busy && m_cancelImmediately) {
        finishOperation();
        return;
    }
    emit changed();
}

void TestWizard::startOperation() {
    m_busy = true;
    if (m_tab) {
        m_tab->setBusy(id(), QStringLiteral("Working…"), busyText, [this] { cancelOperation(); });
    }
    emit changed();
}

void TestWizard::finishOperation() {
    if (!m_busy) {
        return;
    }
    m_busy = false;
    if (m_tab) {
        m_tab->clearBusy(id());
    }
    if (m_index == 4) {
        m_index = 5;
    }
    emit changed();
}

void TestWizard::goTo(int stepIndex) {
    m_index = std::clamp(stepIndex, 0, static_cast<int>(stepIds.size()) - 1);
    if (m_index == 2 && m_checks->status(QStringLiteral("probes")) == diag::CheckStatus::Pending) {
        showResults();
    }
    emit changed();
}

void registerTestWizard(AppViewModel& app, const QUrl& embeddedQml) {
    (void)app.wizardRegistry().add(WizardDescriptor{
        .id = WizardId::ConnectDevice,
        .perTab = true,
        // Qt parent ownership: the WizardViewModel.
        .create = [](QObject* parent) -> Wizard* {
            return new TestWizard(parent); // NOLINT(cppcoreguidelines-owning-memory)
        }});
    (void)app.wizardRegistry().addEmbedded(QStringLiteral("test-embedded"), embeddedQml);
}

} // namespace hio::gui::test
