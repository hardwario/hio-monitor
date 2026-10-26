// The wizard framework (plan/features/0007, C1 + C3): WizardViewModel with the test wizard
// (navigation, busy cancel, one wizard at a time, hand-off), WizardLauncher (canOpen),
// CheckListModel, illustration slots, DiagWorker and ReportViewModel (collect → save → copy).

#include "gui_test_support.h"
#include "hio/core/error.h"
#include "hio/device/operation.h"
#include "hio/device/profile.h"
#include "hio/diag/check.h"
#include "pages/builtin_pages.h"
#include "support/test_wizard.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "wizard/check_list_model.h"
#include "wizard/report_view_model.h"
#include "wizard/wizard.h"
#include "wizard/wizard_launcher.h"
#include "wizard/wizard_view_model.h"
#include "workers/diag_worker.h"

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QObject>
#include <QPointer>
#include <QSignalSpy>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtTypes>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace {

using hio::ErrorCode;
using hio::gui::AppViewModel;
using hio::gui::CheckListModel;
using hio::gui::DeviceTabViewModel;
using hio::gui::ErrorInfo;
using hio::gui::StepKind;
using hio::gui::WizardViewModel;
using hio::gui::test::TestWizard;
using hio::gui::test::waitFor;

class WizardTest : public hio::gui::test::GuiTest {
protected:
    [[nodiscard]] std::unique_ptr<AppViewModel> makeWizardApp(bool withWizard = true) const {
        return std::make_unique<AppViewModel>(hio::gui::AppOptions{
            .settings = openSettings(),
            .registry = registryOptions(),
            .scanInterval = std::chrono::milliseconds(200),
            .registerExtensions =
                [withWizard](AppViewModel& app) {
                    hio::gui::registerBuiltInPages(app);
                    if (withWizard) {
                        hio::gui::test::registerTestWizard(
                            app, QUrl(QStringLiteral("qrc:/TestEmbedded.qml")));
                    }
                },
            .crashDirectory = {}});
    }

    static TestWizard* testWizard(AppViewModel& app) {
        return qobject_cast<TestWizard*>(app.wizard()->wizard());
    }

    static int stepKind(const WizardViewModel& wizard) {
        return wizard.step().value(QStringLiteral("kind")).toInt();
    }
};

TEST_F(WizardTest, LauncherOffersOnlyRegisteredWizards) {
    const auto app = makeWizardApp();
    EXPECT_TRUE(app->launcher()->canOpen(QStringLiteral("connect-device")));
    EXPECT_FALSE(app->launcher()->canOpen(QStringLiteral("troubleshooting")));
    EXPECT_FALSE(app->launcher()->canOpen(QStringLiteral("first-run-setup")));
    EXPECT_FALSE(app->launcher()->canOpen(QString()));
    EXPECT_FALSE(app->launcher()->canOpen(QStringLiteral("nonsense")));
    EXPECT_FALSE(app->launcher()->start(QStringLiteral("troubleshooting")));
    EXPECT_FALSE(app->wizard()->isOpen());

    const auto without = makeWizardApp(false);
    EXPECT_FALSE(without->launcher()->canOpen(QStringLiteral("connect-device")));
}

TEST_F(WizardTest, NavigationThroughEveryStepKind) {
    const auto app = makeWizardApp();
    WizardViewModel& wizard = *app->wizard();
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    ASSERT_TRUE(wizard.isOpen());
    EXPECT_EQ(wizard.wizardId(), QStringLiteral("connect-device"));
    EXPECT_EQ(wizard.title(), QStringLiteral("Test wizard"));
    EXPECT_EQ(wizard.tab(), app->currentTabViewModel());
    EXPECT_EQ(stepKind(wizard), StepKind::Info);
    EXPECT_FALSE(wizard.canBack()); // hidden on the first step
    EXPECT_EQ(wizard.currentIndex(), 0);
    EXPECT_EQ(wizard.steps().size(), 6);
    EXPECT_EQ(wizard.steps().at(0).toMap().value(QStringLiteral("state")),
              QStringLiteral("current"));
    EXPECT_EQ(wizard.stepLabel(), QStringLiteral("Step 1 of 6 · Probe"));
    EXPECT_EQ(wizard.stepAnnouncement(), QStringLiteral("Step 1 of 6, Probe"));

    wizard.primary();
    EXPECT_EQ(stepKind(wizard), StepKind::Choice);
    EXPECT_TRUE(wizard.canBack());
    EXPECT_EQ(wizard.steps().at(0).toMap().value(QStringLiteral("state")), QStringLiteral("done"));
    wizard.choose(QStringLiteral("lte"));
    const QVariantList choices = wizard.step().value(QStringLiteral("choices")).toList();
    ASSERT_EQ(choices.size(), 2);
    EXPECT_TRUE(choices.at(1).toMap().value(QStringLiteral("selected")).toBool());

    wizard.primary();
    EXPECT_EQ(stepKind(wizard), StepKind::Checks);
    CheckListModel* checks = wizard.checks();
    ASSERT_NE(checks, nullptr);
    EXPECT_EQ(checks->count(), 2);
    EXPECT_TRUE(checks->anyFailed());
    EXPECT_EQ(wizard.step().value(QStringLiteral("primaryText")), QStringLiteral("Check again"));
    wizard.primary(); // Check again
    EXPECT_TRUE(checks->allPassed());
    EXPECT_EQ(wizard.step().value(QStringLiteral("primaryText")), QStringLiteral("Next"));

    wizard.back();
    EXPECT_EQ(stepKind(wizard), StepKind::Choice);
    wizard.primary();
    wizard.primary();
    EXPECT_EQ(stepKind(wizard), StepKind::Commands);
    EXPECT_EQ(wizard.step().value(QStringLiteral("commands")).toStringList().size(), 2);

    wizard.primary();
    EXPECT_EQ(stepKind(wizard), StepKind::Embedded);
    EXPECT_EQ(wizard.embeddedSource(), QUrl(QStringLiteral("qrc:/TestEmbedded.qml")));
    ASSERT_NE(wizard.embedded(), nullptr);

    wizard.primary(); // "Flash": the operation finishes at once in this test
    ASSERT_TRUE(testWizard(*app)->busy());
    testWizard(*app)->finishOperation();
    EXPECT_EQ(stepKind(wizard), StepKind::Done);
    QSignalSpy handOff(&wizard, &WizardViewModel::handOff);
    wizard.action(QStringLiteral("open-console"));
    EXPECT_EQ(handOff.count(), 1);
    wizard.primary(); // Finish
    EXPECT_FALSE(wizard.isOpen());
}

TEST_F(WizardTest, ErrorContextStartsAtTheExplainingStep) {
    const auto app = makeWizardApp();
    const ErrorInfo error(hio::makeError(ErrorCode::ProbeNotFound));
    ASSERT_EQ(error.wizard(), QStringLiteral("connect-device"));
    ASSERT_TRUE(app->launcher()->open(error.wizard(), error, 0));
    EXPECT_EQ(stepKind(*app->wizard()), StepKind::Checks);
    EXPECT_EQ(testWizard(*app)->context().code(), QStringLiteral("probe.not_found"));
}

TEST_F(WizardTest, OnlyOneWizardAtATime) {
    const auto app = makeWizardApp();
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    QSignalSpy raised(app->wizard(), &WizardViewModel::raiseRequested);
    EXPECT_FALSE(app->launcher()->start(QStringLiteral("connect-device")));
    EXPECT_EQ(raised.count(), 1);
    EXPECT_TRUE(app->wizard()->isOpen());
}

TEST_F(WizardTest, CancelWhileBusyAsksFirst) {
    const auto app = makeWizardApp();
    WizardViewModel& wizard = *app->wizard();
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    DeviceTabViewModel* tab = app->currentTabViewModel();
    testWizard(*app)->goTo(4);
    testWizard(*app)->setCancelFinishesImmediately(false);
    wizard.primary(); // the operation starts
    EXPECT_EQ(wizard.busyCancelText(), TestWizard::busyText);
    EXPECT_TRUE(tab->isBusy());
    EXPECT_EQ(tab->busyPage(), QStringLiteral("connect-device"));
    EXPECT_FALSE(wizard.canBack()); // hidden while busy

    QSignalSpy confirm(&wizard, &WizardViewModel::cancelConfirmationNeeded);
    wizard.cancel(false);
    ASSERT_EQ(confirm.count(), 1);
    EXPECT_EQ(confirm.at(0).at(0).toString(), TestWizard::busyText);
    EXPECT_TRUE(wizard.isOpen());

    wizard.cancel(true); // [Cancel and close]: waits for the operation to end
    EXPECT_TRUE(wizard.closing());
    EXPECT_TRUE(wizard.isOpen());
    EXPECT_EQ(testWizard(*app)->cancelCalls(), 1);
    testWizard(*app)->finishOperation();
    EXPECT_FALSE(wizard.isOpen());
    EXPECT_FALSE(tab->isBusy());
}

TEST_F(WizardTest, CancelWhenIdleClosesAtOnce) {
    const auto app = makeWizardApp();
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    QSignalSpy confirm(app->wizard(), &WizardViewModel::cancelConfirmationNeeded);
    app->wizard()->cancel(false);
    EXPECT_EQ(confirm.count(), 0);
    EXPECT_FALSE(app->wizard()->isOpen());
    // The wizard object is deleted on the next event-loop turn.
    EXPECT_TRUE(waitFor([&] { return app->wizard()->findChildren<TestWizard*>().isEmpty(); }));
}

TEST_F(WizardTest, ClosingTheTabClosesItsWizard) {
    const auto app = makeWizardApp();
    app->newTab();
    ASSERT_EQ(app->tabs()->count(), 2);
    DeviceTabViewModel* tab = app->tabs()->at(1);
    ASSERT_TRUE(tab->configure(QStringLiteral("chester"), QStringLiteral("app"), 801000001));
    ASSERT_TRUE(tab->ensureSession(false)); // a wizard's probe-only session
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device"), 1));
    EXPECT_EQ(app->wizard()->tab(), tab);
    const QPointer<TestWizard> wizard = testWizard(*app);
    ASSERT_FALSE(wizard.isNull());
    app->closeTab(1);
    // Review V2: closed at once, before the tab shut its session down (not on `destroyed`).
    EXPECT_FALSE(app->wizard()->isOpen());
    ASSERT_FALSE(wizard.isNull()); // deleted later
    EXPECT_EQ(wizard->cancelCalls(), 1);
    EXPECT_TRUE(wizard->tabSessionAtCancel());
    EXPECT_TRUE(waitFor([&] { return wizard.isNull(); }));
}

TEST_F(WizardTest, ClosingAnotherTabKeepsTheWizard) {
    const auto app = makeWizardApp();
    app->newTab();
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device"), 0));
    app->closeTab(1);
    EXPECT_TRUE(app->wizard()->isOpen());
    EXPECT_EQ(app->wizard()->tab(), app->tabs()->at(0));
}

TEST_F(WizardTest, OpenWizardFromTheTab) {
    const auto app = makeWizardApp();
    DeviceTabViewModel* tab = app->currentTabViewModel();
    tab->openWizard(QStringLiteral("connect-device"));
    ASSERT_TRUE(app->wizard()->isOpen());
    EXPECT_EQ(app->wizard()->tab(), tab);
}

TEST_F(WizardTest, CopyTextGoesToTheClipboard) {
    const auto app = makeWizardApp();
    ASSERT_TRUE(app->launcher()->start(QStringLiteral("connect-device")));
    app->wizard()->copyText(QStringLiteral("sudo udevadm trigger"));
    QClipboard* clipboard = QGuiApplication::clipboard();
    ASSERT_NE(clipboard, nullptr);
    EXPECT_EQ(clipboard->text(), QStringLiteral("sudo udevadm trigger"));
}

TEST(CheckListModelTest, RowsAndStatuses) {
    CheckListModel model;
    const std::array ids{QStringLiteral("a"), QStringLiteral("b")};
    const std::array titles{QStringLiteral("A"), QStringLiteral("B")};
    model.reset(ids, titles);
    EXPECT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.data(model.index(0), CheckListModel::StatusRole), QStringLiteral("pending"));
    QSignalSpy announced(&model, &CheckListModel::resultAnnounced);
    model.update(hio::diag::CheckResult{.id = "a",
                                        .status = hio::diag::CheckStatus::Running,
                                        .title = {},
                                        .result = {},
                                        .error = std::nullopt});
    EXPECT_TRUE(model.running());
    EXPECT_EQ(announced.count(), 0);
    model.update(hio::device::DiagnoseStep{.id = "a",
                                           .status = hio::diag::CheckStatus::Failed,
                                           .result = "no power",
                                           .error = hio::makeError(ErrorCode::TargetNoPower)});
    EXPECT_FALSE(model.running());
    EXPECT_TRUE(model.anyFailed());
    EXPECT_EQ(model.data(model.index(0), CheckListModel::TitleRole), QStringLiteral("A"));
    EXPECT_EQ(model.data(model.index(0), CheckListModel::ResultRole), QStringLiteral("no power"));
    const auto error = model.data(model.index(0), CheckListModel::ErrorRole).value<ErrorInfo>();
    EXPECT_EQ(error.code(), QStringLiteral("target.no_power"));
    ASSERT_EQ(announced.count(), 1);
    EXPECT_EQ(announced.at(0).at(0).toString(), QStringLiteral("A: no power"));
    model.update(hio::diag::CheckResult{.id = "unknown",
                                        .status = hio::diag::CheckStatus::Ok,
                                        .title = {},
                                        .result = {},
                                        .error = std::nullopt});
    EXPECT_EQ(model.rowCount(), 2);
    model.setAllPending();
    EXPECT_FALSE(model.anyFailed());
    // Bounded: at most diag::maxChecks rows.
    std::vector<QString> many(20, QStringLiteral("x"));
    model.reset(many, {});
    EXPECT_EQ(model.rowCount(), static_cast<int>(hio::diag::maxChecks));
}

TEST(WizardIllustrations, EverySlotAndEveryGuideStepResolvesToAFile) {
    for (const QString& slot : WizardViewModel::illustrationSlots()) {
        EXPECT_TRUE(WizardViewModel::hasIllustration(slot)) << slot.toStdString();
    }
    for (const auto* profile : hio::device::ProfileRegistry::all()) {
        for (const auto& target : profile->targets) {
            for (const auto& step : target.connectionGuide) {
                const QString slot =
                    QString::fromUtf8(step.illustrationSlot.data(),
                                      static_cast<qsizetype>(step.illustrationSlot.size()));
                EXPECT_TRUE(WizardViewModel::illustrationSlots().contains(slot))
                    << slot.toStdString();
                EXPECT_TRUE(WizardViewModel::hasIllustration(slot)) << slot.toStdString();
            }
        }
    }
    EXPECT_TRUE(WizardViewModel::illustrationUrl(QStringLiteral("no-such-slot"), false)
                    .toString()
                    .endsWith(QStringLiteral("/placeholder.svg")));
    // No dark variant yet: the light one is used.
    EXPECT_TRUE(WizardViewModel::illustrationUrl(QStringLiteral("jlink-usb"), true)
                    .toString()
                    .endsWith(QStringLiteral("/jlink-usb.svg")));
}

TEST_F(WizardTest, DiagWorkerRunsChecksWithGenerations) {
    hio::gui::DiagWorker worker;
    hio::gui::DiagWorker::ChecksRequest request;
    request.context.jlink.option = std::filesystem::path(HIO_FAKE_JLINK_LIBRARY);
    request.context.instanceRoot = root() / "instances";
    request.context.lockDirectory = root() / "locks";
    request.context.settingsDir = root() / "settings";
    request.context.logDir = root() / "logs";
    request.ids = {"jlink-software", "probes"};
    std::vector<hio::diag::CheckResult> finished;
    std::uint64_t finishedGeneration = 0;
    int progress = 0;
    QObject::connect(&worker, &hio::gui::DiagWorker::checkProgress,
                     [&progress](quint64, const hio::diag::CheckResult&) { ++progress; });
    QObject::connect(&worker, &hio::gui::DiagWorker::checksFinished,
                     [&](quint64 generation, const std::vector<hio::diag::CheckResult>& results) {
                         finishedGeneration = generation;
                         finished = results;
                     });
    const std::uint64_t generation = worker.runChecks(request);
    EXPECT_GT(generation, 0U);
    ASSERT_TRUE(waitFor([&] { return finishedGeneration == generation; }));
    ASSERT_EQ(finished.size(), 2U);
    EXPECT_EQ(finished[0].id, "jlink-software");
    EXPECT_EQ(finished[0].status, hio::diag::CheckStatus::Ok);
    EXPECT_EQ(finished[1].result, "2 found: 801000001, 801000002");
    EXPECT_EQ(progress, 4); // Running + result per check
    // A cancelled pending request never finishes.
    const std::uint64_t second = worker.runChecks(request);
    worker.cancel(second);
    const std::uint64_t third = worker.runChecks(request);
    ASSERT_TRUE(waitFor([&] { return finishedGeneration == third; }));
}

// Review V3: a subset of the checks runs with the checks it depends on (transitively), in order.
TEST_F(WizardTest, DiagWorkerAddsTheDependenciesOfRequestedChecks) {
    hio::gui::DiagWorker worker;
    hio::gui::DiagWorker::ChecksRequest request;
    request.context.jlink.option = std::filesystem::path(HIO_FAKE_JLINK_LIBRARY);
    request.context.instanceRoot = root() / "instances";
    request.context.lockDirectory = root() / "locks";
    request.context.settingsDir = root() / "settings";
    request.context.logDir = root() / "logs";
    request.ids = {"probe-firmware"};
    std::vector<hio::diag::CheckResult> finished;
    std::uint64_t finishedGeneration = 0;
    QObject::connect(&worker, &hio::gui::DiagWorker::checksFinished,
                     [&](quint64 generation, const std::vector<hio::diag::CheckResult>& results) {
                         finishedGeneration = generation;
                         finished = results;
                     });
    const std::uint64_t generation = worker.runChecks(request);
    ASSERT_TRUE(waitFor([&] { return finishedGeneration == generation; }));
    ASSERT_EQ(finished.size(), 3U);
    EXPECT_EQ(finished[0].id, "jlink-software");
    EXPECT_EQ(finished[1].id, "probes");
    EXPECT_EQ(finished[2].id, "probe-firmware");
    EXPECT_EQ(finished[2].status, hio::diag::CheckStatus::Ok);
}

TEST_F(WizardTest, ReportCollectSaveAndCopy) {
    const auto app = makeWizardApp();
    hio::gui::ReportViewModel& report = *app->report();
    report.show();
    EXPECT_TRUE(report.isOpen());
    EXPECT_EQ(report.state(), QStringLiteral("collecting"));
    EXPECT_TRUE(report.defaultFileName().startsWith(QStringLiteral("hio-diagnostics-")));
    ASSERT_TRUE(waitFor([&] { return report.state() == QStringLiteral("ready"); },
                        std::chrono::seconds(30)));
    EXPECT_TRUE(report.summary().contains(QStringLiteral("HARDWARIO Monitor diagnostic report")));
    EXPECT_TRUE(report.summary().contains(QStringLiteral("801000001")));

    report.copySummary();
    EXPECT_EQ(QGuiApplication::clipboard()->text(), report.summary());

    const QString target = QString::fromStdU16String((root() / "report.zip").u16string());
    report.save(QUrl::fromLocalFile(target));
    EXPECT_EQ(report.state(), QStringLiteral("saving"));
    ASSERT_TRUE(waitFor([&] { return report.state() == QStringLiteral("saved"); }));
    EXPECT_TRUE(QFile::exists(target));
    EXPECT_TRUE(app->toasts().contains(QStringLiteral("Diagnostic report saved.")));

    // A folder that does not exist: diag.report_write_failed in the dialog.
    report.save(
        QUrl::fromLocalFile(QString::fromStdU16String((root() / "missing" / "r.zip").u16string())));
    ASSERT_TRUE(waitFor([&] { return report.state() == QStringLiteral("error"); }));
    EXPECT_EQ(report.error().code(), QStringLiteral("diag.report_write_failed"));
    report.close();
    EXPECT_FALSE(report.isOpen());
}

TEST_F(WizardTest, ReportCancelWhileCollecting) {
    const auto app = makeWizardApp();
    hio::gui::ReportViewModel& report = *app->report();
    report.show();
    report.close();
    EXPECT_FALSE(report.isOpen());
    hio::gui::test::spin(std::chrono::milliseconds(300));
    EXPECT_FALSE(report.isOpen());
    // It can be opened again.
    report.show();
    ASSERT_TRUE(waitFor([&] { return report.state() == QStringLiteral("ready"); },
                        std::chrono::seconds(30)));
}

} // namespace
