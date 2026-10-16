#pragma once

// The test wizard of plan 0007 (C1): exercises the wizard contract with every step kind
// (Info → Choice → Checks → Commands → Embedded → Done), an operation that keeps the tab busy
// (busyCancelText / cancelOperation) and a hand-off. Registered by the GUI tests under the id
// "connect-device" (the real Connect wizard is plan 0009). Tests only; texts are not translated.

#include "wizard/check_list_model.h"
#include "wizard/wizard.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <vector>

namespace hio::gui {

class AppViewModel;
class DeviceTabViewModel;

namespace test {

class TestWizardEmbedded : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text CONSTANT)

public:
    using QObject::QObject;
    [[nodiscard]] static QString text() { return QStringLiteral("Embedded control"); }
};

class TestWizard : public Wizard {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int cancelCalls READ cancelCalls NOTIFY changed)
    Q_PROPERTY(bool cancelFinishesImmediately READ cancelFinishesImmediately WRITE
                   setCancelFinishesImmediately NOTIFY changed)

public:
    static inline const QString busyText =
        QStringLiteral("The operation is in progress. Cancel it and close the wizard?");

    explicit TestWizard(QObject* parent);

    [[nodiscard]] QString id() const override { return QStringLiteral("connect-device"); }
    [[nodiscard]] QString title() const override { return QStringLiteral("Test wizard"); }
    void start(const ErrorInfo& context, DeviceTabViewModel* tab) override;
    [[nodiscard]] std::vector<WizardStep> plannedSteps() const override;
    [[nodiscard]] WizardStep current() const override;
    void primary() override;
    void back() override;
    void choose(const QString& choiceId) override;
    [[nodiscard]] bool canBack() const override;
    [[nodiscard]] bool canPrimary() const override;
    [[nodiscard]] QString busyCancelText() const override;
    void cancelOperation() override;
    [[nodiscard]] CheckListModel* checks() override { return m_checks; }
    [[nodiscard]] QObject* embeddedViewModel() override { return m_embedded; }

    // Test hooks.
    Q_INVOKABLE void startOperation(); // the tab becomes busy (page id = wizard id)
    Q_INVOKABLE void finishOperation();
    Q_INVOKABLE void goTo(int stepIndex);
    [[nodiscard]] bool busy() const noexcept { return m_busy; }
    [[nodiscard]] int cancelCalls() const noexcept { return m_cancelCalls; }
    // At the last cancelOperation(): the tab was still there and had a session (review V2: the
    // wizard is closed before the tab shuts its session down).
    [[nodiscard]] bool tabSessionAtCancel() const noexcept { return m_tabSessionAtCancel; }
    [[nodiscard]] int stepIndex() const noexcept { return m_index; }
    [[nodiscard]] const ErrorInfo& context() const noexcept { return m_context; }
    // cancelOperation() ends the operation at once (default) or waits for finishOperation().
    [[nodiscard]] bool cancelFinishesImmediately() const noexcept { return m_cancelImmediately; }
    void setCancelFinishesImmediately(bool value) { m_cancelImmediately = value; }

private:
    void showResults();

    CheckListModel* m_checks;       // child
    TestWizardEmbedded* m_embedded; // child
    // The wizard contract: the tab only as QPointer (it may close while the wizard is open).
    QPointer<DeviceTabViewModel> m_tab;
    ErrorInfo m_context;
    QString m_choice = QStringLiteral("app");
    int m_index = 0;
    bool m_busy = false;
    int m_cancelCalls = 0;
    bool m_tabSessionAtCancel = false;
    bool m_cancelImmediately = true;
};

// Registers the test wizard (id "connect-device", per tab) and its embedded control
// "test-embedded" (`embeddedQml`).
void registerTestWizard(AppViewModel& app, const QUrl& embeddedQml);

} // namespace test
} // namespace hio::gui
