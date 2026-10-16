// Screenshots of the shell (plan 0004, B6) in both themes and both languages; only when
// HIO_SCREENSHOT_DIR is set (QT_QUICK_BACKEND=software for the offscreen platform).
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    function initTestCase() {
        if (testSupport.screenshotDir === "")
            skip("HIO_SCREENSHOT_DIR is not set");
        window = Qt.createComponent("HardwarioMonitor", "Main").createObject(testCase);
        verify(window !== null);
    }

    function cleanupTestCase() {
        if (window)
            window.destroy();
        wait(50); // the deferred delete runs before the engine goes away
        testSupport.resetTabs();
        AppViewModel.theme = "system";
        AppViewModel.setLanguage("en");
    }

    function visibleAll(parent, name) {
        let result = [];
        const children = parent && parent.children ? parent.children : [];
        for (let i = 0; i < children.length; ++i) {
            let shown = true;
            for (let item = children[i]; item; item = item.parent)
                shown = shown && item.visible;
            if (children[i].objectName === name && shown)
                result.push(children[i]);
            result = result.concat(visibleAll(children[i], name));
        }
        return result;
    }

    function save(name) {
        waitForRendering(window.contentItem, 2000);
        wait(250);
        const image = grabImage(window.contentItem);
        verify(image.width > 0, name);
        const file = testSupport.screenshotDir + "/" + name + "-" + AppViewModel.theme + "-" + AppViewModel.language + ".png";
        image.save(file); // throws on failure
    }

    function test_shots_data() {
        return [{ tag: "light-en", theme: "light", language: "en" }, { tag: "dark-en", theme: "dark", language: "en" },
                { tag: "light-cs", theme: "light", language: "cs" }, { tag: "dark-cs", theme: "dark", language: "cs" }];
    }

    function test_shots(data) {
        if (testSupport.screenshotDir === "")
            skip("HIO_SCREENSHOT_DIR is not set");
        AppViewModel.theme = data.theme;
        AppViewModel.setLanguage(data.language);
        testSupport.resetTabs();
        window.width = 1280;
        window.height = 800;
        tryVerify(() => AppViewModel.currentTabViewModel.picker.count === 2, 5000);
        save("01-choose-device");

        verify(testSupport.configureTab(0, "app", 801000001, "Test Probe"));
        AppViewModel.newTab();
        verify(testSupport.configureTab(1, "lte", 801000002, "Test Probe 2"));
        AppViewModel.currentTab = 0;
        AppViewModel.currentTabViewModel.connection.connectDevice();
        tryCompare(AppViewModel.currentTabViewModel, "state", DeviceTabViewModel.Connected, 10000);
        save("02-connected");

        testSupport.testPage(0).startWork();
        AppViewModel.showPage("flash");
        save("03-busy");
        testSupport.finishWork(0);
        AppViewModel.currentTabViewModel.connection.disconnectDevice();

        AppViewModel.currentTab = 1;
        testSupport.setConnectionError(1, "probe.not_found");
        save("04-error");

        AppViewModel.shortcutOverlayVisible = true;
        save("05-shortcuts");
        AppViewModel.shortcutOverlayVisible = false;

        AppViewModel.openGlobalPage("settings");
        save("06-settings");
        AppViewModel.closeGlobalPage();

        // The ⋯ Advanced popover (ADR-0096): automatic speed, then a slow fixed speed (ADR-0095).
        AppViewModel.currentTab = 0;
        const connection = AppViewModel.currentTabViewModel.connection;
        connection.autoSpeed = true;
        const button = visibleAll(window.contentItem, "advancedButton")[0];
        verify(button !== undefined);
        let popover = null;
        for (let item = button; item && !popover; item = item.parent)
            popover = findChild(item, "advancedPopover");
        verify(popover !== null);
        mouseClick(button);
        tryVerify(() => popover.opened);
        save("08-advanced-auto");
        connection.autoSpeed = false;
        connection.speedKhz = 100;
        save("09-advanced-slow");
        connection.autoSpeed = true;
        keyClick(Qt.Key_Escape);
        tryVerify(() => !popover.visible);

        // M4 (plan 0007): the wizard dialog with the test wizard, its check list and the
        // diagnostic report dialog.
        verify(AppViewModel.launcher.start("connect-device", 0));
        save("10-wizard-info");
        testSupport.openWizard().goTo(2);
        save("11-wizard-checks");
        testSupport.openWizard().goTo(3);
        save("12-wizard-commands");
        testSupport.openWizard().goTo(5);
        save("13-wizard-done");
        AppViewModel.wizard.cancel(true);
        tryVerify(() => !AppViewModel.wizard.open);
        AppViewModel.report.show();
        tryCompare(AppViewModel.report, "state", "ready", 30000);
        save("14-report");
        AppViewModel.report.close();

        window.width = 960;
        window.height = 600;
        save("07-minimum-960x600");
        verify(AppViewModel.launcher.start("connect-device", 0));
        testSupport.openWizard().goTo(2);
        save("15-wizard-checks-960x600");
        AppViewModel.wizard.cancel(true);
        tryVerify(() => !AppViewModel.wizard.open);
        testSupport.resetTabs();
    }

    name: "Screenshots"
    when: windowShown
}
