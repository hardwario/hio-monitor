// Base components in light and dark (plan 0004, A3): HButton, HToolButton, ErrorPanel,
// WarningBanner, Toast, ConfirmDialog, EmptyState, ShortcutOverlay, PhaseStepper.
import QtQuick
import QtQuick.Controls
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    width: 900
    height: 700
    visible: true

    Component {
        id: buttonComponent

        HButton {}
    }

    Component {
        id: toolButtonComponent

        HToolButton {}
    }

    Component {
        id: errorPanelComponent

        ErrorPanel {
            width: 700
        }
    }

    Component {
        id: bannerComponent

        WarningBanner {
            width: 700
        }
    }

    Component {
        id: dialogComponent

        ConfirmDialog {}
    }

    Component {
        id: emptyComponent

        EmptyState {}
    }

    Component {
        id: stepperComponent

        PhaseStepper {
            width: 700
        }
    }

    Component {
        id: toastComponent

        Toast {}
    }

    Component {
        id: overlayComponent

        ShortcutOverlay {}
    }

    function themes() {
        return [{ tag: "light", theme: "light" }, { tag: "dark", theme: "dark" }];
    }

    function cleanupTestCase() {
        AppViewModel.theme = "system";
        AppViewModel.shortcutOverlayVisible = false;
    }

    function setTheme(theme) {
        AppViewModel.theme = theme;
        compare(Theme.dark, theme === "dark");
    }

    function test_buttons_data() {
        return themes();
    }

    function test_buttons(data) {
        setTheme(data.theme);
        const primary = createTemporaryObject(buttonComponent, testCase, { kind: "primary", text: "Connect" });
        const danger = createTemporaryObject(buttonComponent, testCase, { kind: "danger", text: "Erase everything", y: 80 });
        const normal = createTemporaryObject(buttonComponent, testCase, { text: "Retry", iconName: "refresh", y: 40 });
        compare(primary.contentColor, Theme.onAccent);
        compare(danger.contentColor, Theme.onAccent);
        compare(normal.contentColor, Theme.text);
        compare(primary.height, Theme.controlHeight);
        verify(primary.width >= primary.implicitWidth - 0.5);
        compare(primary.Accessible.name, "Connect");
        let clicked = 0;
        primary.clicked.connect(() => ++clicked);
        mouseClick(primary);
        compare(clicked, 1);
        const tool = createTemporaryObject(toolButtonComponent, testCase,
                                           { iconName: "settings", text: "Settings", shortcutId: "settings.open", y: 120 });
        compare(tool.display, AbstractButton.TextBesideIcon);
        tool.compact = true;
        compare(tool.display, AbstractButton.IconOnly);
        verify(tool.ToolTip.text.indexOf("Settings (") === 0, tool.ToolTip.text);
        compare(tool.Accessible.name, "Settings");
        waitForRendering(tool);
    }

    function test_errorPanel_data() {
        return themes();
    }

    function test_errorPanel(data) {
        setTheme(data.theme);
        const panel = createTemporaryObject(errorPanelComponent, testCase,
                                            { error: testSupport.error("target.no_power", true) });
        verify(panel.visible);
        compare(panel.color, Theme.errorPanelBg);
        compare(findChild(panel, "errorHeadline").text, "The device is not powered.");
        compare(findChild(panel, "errorCause").text, "Probably: the target voltage is 0.0 V.");
        compare(findChild(panel, "errorSuggestion").text, "What to do: connect power, then reconnect.");
        compare(findChild(panel, "errorPrimary").text, "Reconnect");
        compare(panel.primaryAction, ErrorAction.Reconnect);
        // The wizard button is shown but disabled ("Coming soon", M4).
        const wizard = findChild(panel, "errorWizard");
        verify(wizard.visible);
        compare(wizard.children[0].enabled, false);
        compare(wizard.ToolTip.text, "Coming soon");
        // Details: code and detail, collapsed at first.
        verify(!findChild(panel, "errorDetails").visible);
        mouseClick(findChild(panel, "errorDetailsToggle"));
        verify(findChild(panel, "errorDetails").visible);
        compare(findChild(panel, "errorDetailsText").text, "target.no_power\nVTref = 0 mV · J-Link 801000001");
        let action = -1;
        panel.actionTriggered.connect(value => action = value);
        mouseClick(findChild(panel, "errorPrimary"));
        compare(action, ErrorAction.Reconnect);
        // A page overrides the primary action.
        panel.primaryAction = ErrorAction.ChooseFile;
        compare(findChild(panel, "errorPrimary").text, "Choose another file");
        // Compact (window height < 700): headline + primary action + chevron.
        panel.compact = true;
        verify(!findChild(panel, "errorCause").visible);
        verify(findChild(panel, "errorPrimaryCompact").visible);
        waitForRendering(panel);
    }

    function test_errorPanelWithoutWizardOrError() {
        const panel = createTemporaryObject(errorPanelComponent, testCase,
                                            { error: testSupport.error("io.read_failed", false) });
        verify(!findChild(panel, "errorWizard").visible);
        compare(findChild(panel, "errorPrimary").text, "Retry");
        const empty = createTemporaryObject(errorPanelComponent, testCase);
        verify(!empty.visible);
    }

    function test_actionTexts() {
        const panel = createTemporaryObject(errorPanelComponent, testCase,
                                            { error: testSupport.error("dependency.jlink_not_found", false) });
        compare(findChild(panel, "errorPrimary").text, "Open Settings");
        panel.primaryAction = ErrorAction.Dismiss;
        compare(findChild(panel, "errorPrimary").text, "Dismiss");
    }

    function test_warningBanner_data() {
        return themes();
    }

    function test_warningBanner(data) {
        setTheme(data.theme);
        const banner = createTemporaryObject(bannerComponent, testCase, { text: "312 lines were dropped." });
        compare(banner.color, Theme.warningPanelBg);
        compare(findChild(banner, "bannerText").text, "312 lines were dropped.");
        let dismissed = 0;
        banner.dismissed.connect(() => ++dismissed);
        mouseClick(findChild(banner, "bannerDismiss"));
        compare(dismissed, 1);
        waitForRendering(banner);
    }

    function test_confirmDialog_data() {
        return themes();
    }

    function test_confirmDialog(data) {
        setTheme(data.theme);
        const dialog = createTemporaryObject(dialogComponent, testCase, {
            title: "Erase everything on the application MCU?",
            body: "This erases:",
            items: ["Firmware (flash)", "UICR configuration"],
            consequence: "This cannot be undone.",
            acceptText: "Erase everything"
        });
        let accepted = 0;
        let rejected = 0;
        dialog.accepted.connect(() => ++accepted);
        dialog.rejected.connect(() => ++rejected);
        dialog.open();
        tryVerify(() => dialog.opened);
        const reject = findChild(dialog, "dialogReject");
        tryVerify(() => reject.activeFocus); // Cancel is the default
        compare(findChild(dialog, "dialogAccept").kind, "danger");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !dialog.visible);
        compare(rejected, 1);
        dialog.open();
        tryVerify(() => dialog.opened);
        mouseClick(findChild(dialog, "dialogAccept"));
        tryVerify(() => !dialog.visible);
        compare(accepted, 1);
    }

    function test_emptyState_data() {
        return themes();
    }

    function test_emptyState(data) {
        setTheme(data.theme);
        const empty = createTemporaryObject(emptyComponent, testCase, {
            iconName: "probe", headline: "No J-Link probe found", text: "Connect the J-Link.", actionText: "Refresh"
        });
        compare(findChild(empty, "emptyHeadline").text, "No J-Link probe found");
        let triggered = 0;
        empty.actionTriggered.connect(() => ++triggered);
        mouseClick(findChild(empty, "emptyAction"));
        compare(triggered, 1);
    }

    function test_phaseStepper_data() {
        return themes();
    }

    function test_phaseStepper(data) {
        setTheme(data.theme);
        const stepper = createTemporaryObject(stepperComponent, testCase, {
            phases: [{ title: "Connecting", state: "done" }, { title: "Programming", state: "current" },
                     { title: "Verifying", state: "pending" }],
            progress: 0.51,
            detailText: "412 / 803 kB · 51 % · 0:07"
        });
        const progress = findChild(stepper, "phaseProgress");
        compare(progress.value, 0.51);
        compare(progress.indeterminate, false);
        stepper.progress = -1;
        compare(progress.indeterminate, true);
        let cancelled = 0;
        stepper.cancelRequested.connect(() => ++cancelled);
        mouseClick(findChild(stepper, "phaseCancel"));
        compare(cancelled, 1);
        waitForRendering(stepper);
    }

    function test_toast() {
        const toasts = createTemporaryObject(toastComponent, testCase);
        AppViewModel.showToast("Firmware flashed and verified in 14 s.");
        tryVerify(() => findChild(toasts, "toast") !== null);
        compare(findChild(toasts, "toast").color, Theme.text);
    }

    function test_shortcutOverlay() {
        const overlay = createTemporaryObject(overlayComponent, testCase);
        AppViewModel.shortcutOverlayVisible = true;
        tryVerify(() => overlay.opened);
        verify(AppViewModel.shortcuts.groups.length >= 2);
        verify(findChild(overlay.contentItem, "shortcutKeys") !== null);
        keyClick(Qt.Key_Escape);
        tryCompare(AppViewModel, "shortcutOverlayVisible", false);
    }

    name: "Components"
    when: windowShown
}
