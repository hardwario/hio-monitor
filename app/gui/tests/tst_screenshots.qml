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

        window.width = 960;
        window.height = 600;
        save("07-minimum-960x600");
        testSupport.resetTabs();
    }

    name: "Screenshots"
    when: windowShown
}
