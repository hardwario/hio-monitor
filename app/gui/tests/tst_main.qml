// The main window: title, sizes, first start, rail, live language switch (plan 0004).
import QtQuick
import QtTest
import HardwarioMonitor

TestCase {
    id: testCase

    property var window: null

    function find(name) {
        return findChild(window.contentItem, name);
    }

    function initTestCase() {
        AppViewModel.setLanguage("en");
        testSupport.resetTabs();
        const component = Qt.createComponent("HardwarioMonitor", "Main");
        verify(component.status === Component.Ready, component.errorString());
        window = component.createObject(testCase);
        verify(window !== null);
        waitForRendering(window.contentItem);
    }

    function cleanupTestCase() {
        AppViewModel.setLanguage("en");
        AppViewModel.theme = "system";
        window.destroy();
        wait(50); // the deferred delete runs before the engine goes away
    }

    function test_titleAndSizes() {
        compare(window.title, "HARDWARIO Monitor");
        compare(window.minimumWidth, 960);
        compare(window.minimumHeight, 600);
        compare(window.width, 1280);
        compare(window.height, 800);
    }

    function test_firstStartShowsChooseDevice() {
        compare(AppViewModel.tabs.count, 1);
        const tab = AppViewModel.currentTabViewModel;
        compare(tab.state, DeviceTabViewModel.ChoosingDevice);
        tryCompare(find("chooseTitle"), "text", "Choose a device to connect");
        verify(find("chooseDeviceLabel").visible);
        compare(find("probeChip").visible, false);
        // The fake library lists two probes.
        tryVerify(() => tab.picker.count === 2, 5000);
    }

    function test_languages() {
        const codes = AppViewModel.availableLanguages.map(language => language.code);
        compare(codes, ["system", "en", "cs"]);
    }

    function test_switchToCzechAndBack() {
        AppViewModel.setLanguage("cs");
        compare(AppViewModel.language, "cs");
        tryCompare(find("chooseTitle"), "text", "Vyberte zařízení k připojení", 5000);
        tryCompare(window, "title", "HARDWARIO Monitor");
        compare(AppViewModel.currentTabViewModel.title, "Nové zařízení");
        AppViewModel.setLanguage("en");
        tryCompare(find("chooseTitle"), "text", "Choose a device to connect", 5000);
        compare(AppViewModel.currentTabViewModel.title, "New device");
    }

    function test_unknownLanguageIsIgnored() {
        AppViewModel.setLanguage("xx");
        compare(AppViewModel.language, "en");
    }

    function test_theme() {
        AppViewModel.theme = "dark";
        compare(Theme.dark, true);
        compare(window.color, Theme.bg);
        compare(Theme.bg, "#1e1f26");
        AppViewModel.theme = "light";
        compare(Theme.dark, false);
        compare(Theme.bg, "#ffffff");
        AppViewModel.theme = "system";
    }

    name: "Main"
    when: windowShown
}
