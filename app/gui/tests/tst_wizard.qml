// The wizard dialog with the test wizard (plan/features/0007, C2-C5): every step kind in light and
// dark, keyboard (Enter, Esc, focus on the heading), busy cancel confirmation, the 960 x 600 window
// in Czech (geometry, compact step list), the illustration slots and the report dialog.
import QtQuick
import QtQuick.Controls
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    function initTestCase() {
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
    }

    function cleanupTestCase() {
        if (AppViewModel.wizard.open)
            AppViewModel.wizard.cancel(true);
        if (window)
            window.destroy();
        wait(50);
        testSupport.resetTabs();
        AppViewModel.theme = "system";
        AppViewModel.setLanguage("en");
    }

    function init() {
        window.width = 1280;
        window.height = 800;
        testSupport.resetTabs();
    }

    function cleanup() {
        if (AppViewModel.wizard.open) {
            const wizard = testSupport.openWizard();
            if (wizard && wizard.busy)
                wizard.finishOperation();
            AppViewModel.wizard.cancel(true);
        }
        tryVerify(() => !AppViewModel.wizard.open);
        if (AppViewModel.report.open)
            AppViewModel.report.close();
    }

    function dialog() {
        return findChild(window, "wizardDialog");
    }

    function part(name) {
        return findChild(dialog(), name);
    }

    function openWizard() {
        verify(AppViewModel.launcher.start("connect-device", 0));
        tryVerify(() => dialog().opened);
    }

    function test_everyStepKind_data() {
        return [{ tag: "light", theme: "light" }, { tag: "dark", theme: "dark" }];
    }

    function test_everyStepKind(data) {
        AppViewModel.theme = data.theme;
        openWizard();
        // Info: illustration, heading (focused), body, link; no Back on the first step.
        compare(part("wizardTitle").text, "Test wizard");
        compare(part("wizardHeading").text, "Connect the J-Link");
        tryVerify(() => part("wizardHeading").activeFocus);
        verify(part("wizardIllustration").visible);
        verify(part("wizardLink").visible);
        verify(!part("wizardBack").visible);
        compare(part("wizardPrimary").text, "Next");
        verify(part("wizardStepList").visible);
        compare(dialog().title, "Test wizard");

        // Enter = the default button.
        keyClick(Qt.Key_Return);
        tryCompare(part("wizardHeading"), "text", "Which MCU do you want to connect?");
        verify(part("wizardBack").visible);
        verify(part("wizardChoices").visible);
        verify(!part("wizardIllustration").visible);

        AppViewModel.wizard.primary();
        // Checks: one failed row with cause and suggestion.
        verify(part("wizardChecks").visible);
        compare(part("wizardPrimary").text, "Check again");
        tryVerify(() => findChild(part("wizardChecks"), "checkCause") !== null);
        mouseClick(part("wizardPrimary"));
        compare(part("wizardPrimary").text, "Next");

        AppViewModel.wizard.primary();
        verify(part("wizardCommands").visible);
        verify(findChild(part("wizardCommands"), "commandCopy") !== null);

        AppViewModel.wizard.primary();
        tryVerify(() => findChild(dialog().contentItem, "testEmbedded") !== null);
        compare(findChild(dialog().contentItem, "testEmbedded").text, "Embedded control");
        compare(part("wizardPrimary").text, "Flash");

        AppViewModel.wizard.primary(); // the operation starts
        const wizard = testSupport.openWizard();
        verify(wizard.busy);
        verify(!part("wizardBack").visible);
        compare(AppViewModel.currentTabViewModel.busyPage, "connect-device");
        wizard.finishOperation();
        compare(part("wizardHeading").text, "Connected");
        compare(part("wizardCancel").text, "Close");
        verify(findChild(dialog().footer, "wizardAction") !== null);
        compare(part("wizardPrimary").text, "Finish");
        mouseClick(part("wizardPrimary"));
        tryVerify(() => !dialog().visible);
    }

    function test_escapeWhileBusyAsks() {
        openWizard();
        const wizard = testSupport.openWizard();
        wizard.cancelFinishesImmediately = false;
        wizard.goTo(4);
        AppViewModel.wizard.primary();
        verify(wizard.busy);
        part("wizardHeading").forceActiveFocus();
        keyClick(Qt.Key_Escape);
        const confirm = findChild(dialog(), "wizardCancelConfirm");
        tryVerify(() => confirm.opened);
        compare(findChild(confirm, "dialogAccept").text, "Cancel and close");
        compare(findChild(confirm, "dialogReject").text, "Keep going");
        mouseClick(findChild(confirm, "dialogReject"));
        tryVerify(() => !confirm.visible);
        verify(dialog().visible);
        AppViewModel.wizard.cancel(true);
        verify(AppViewModel.wizard.closing);
        verify(dialog().visible); // until the operation has ended
        wizard.finishOperation();
        tryVerify(() => !dialog().visible);
    }

    function test_escapeWhenIdleCloses() {
        openWizard();
        part("wizardHeading").forceActiveFocus();
        keyClick(Qt.Key_Escape);
        tryVerify(() => !dialog().visible);
    }

    function test_minimumWindowInCzech() {
        AppViewModel.setLanguage("cs");
        window.width = 960;
        window.height = 600;
        openWizard();
        const d = dialog();
        verify(d.width <= 720 && d.height <= 552);
        verify(d.x >= 0 && d.y >= 0);
        verify(d.x + d.width <= window.width && d.y + d.height <= window.height);
        // Every footer button lies inside the dialog.
        for (const name of ["wizardPrimary", "wizardCancel"]) {
            const button = part(name);
            const p = button.mapToItem(d.contentItem.parent, 0, 0);
            verify(p.x >= 0 && p.x + button.width <= d.width + 1, name);
        }
        testSupport.openWizard().goTo(2);
        const steps = part("wizardStepList");
        verify(steps.visible);
        AppViewModel.setLanguage("en");
    }

    function test_compactStepList() {
        openWizard();
        const steps = part("wizardStepList");
        verify(!findChild(steps, "wizardStepCompact").visible || steps.showCompact);
        steps.compact = true;
        verify(findChild(steps, "wizardStepCompact").visible);
        compare(findChild(steps, "wizardStepCompact").text, "Step 1 of 6 · Probe");
        steps.compact = false;
    }

    Component {
        id: illustrationComponent

        Illustration {}
    }

    function test_illustrationSlotsResolve() {
        for (const slot of ["jlink-usb", "chester-app-cable", "chester-lte-cable", "chester-power", "placeholder"]) {
            const item = createTemporaryObject(illustrationComponent, testCase, { slot: slot });
            verify(String(item.source).endsWith("/" + slot + ".svg"), slot);
            const image = findChild(item, "illustrationImage");
            tryCompare(image, "status", Image.Ready);
        }
        const unknown = createTemporaryObject(illustrationComponent, testCase, { slot: "nope" });
        verify(String(unknown.source).endsWith("/placeholder.svg"));
    }

    function test_reportDialog() {
        AppViewModel.report.show();
        const report = findChild(window, "reportDialog");
        tryVerify(() => report.opened);
        tryCompare(AppViewModel.report, "state", "ready", 30000);
        verify(findChild(report, "reportSummaryText").text.indexOf("HARDWARIO Monitor diagnostic report") === 0);
        verify(findChild(report, "reportNote").visible);
        verify(findChild(report, "reportSave").visible);
        verify(findChild(report, "reportCopy").visible);
        mouseClick(findChild(report, "reportClose"));
        tryVerify(() => !report.visible);
        verify(!AppViewModel.report.open);
    }

    name: "Wizard"
    when: windowShown
}
